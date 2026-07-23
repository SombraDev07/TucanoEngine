using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Platform;
using Avalonia.Threading;

namespace TucanoEditor.Views;

public class ViewportControl : NativeControlHost
{
    private IntPtr _childHwnd;
    private IntPtr _engineHwnd;
    private IntPtr _pendingEngineHwnd;
    private bool _attached;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr CreateWindowEx(
        uint dwExStyle, string lpClassName, string lpWindowName,
        uint dwStyle, int x, int y, int nWidth, int nHeight,
        IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern IntPtr SetParent(IntPtr hWndChild, IntPtr hWndNewParent);

    [DllImport("user32.dll")]
    private static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    [DllImport("user32.dll")]
    private static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr hWndInsertAfter,
        int x, int y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    private static extern bool MoveWindow(
        IntPtr hWnd, int x, int y, int nWidth, int nHeight, bool bRepaint);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left, Top, Right, Bottom;
    }

    private const int GWL_STYLE = -16;
    private const int GWL_EXSTYLE = -20;
    private const uint WS_CHILD = 0x40000000;
    private const uint WS_VISIBLE = 0x10000000;
    private const uint WS_CAPTION = 0x00C00000;
    private const uint WS_THICKFRAME = 0x00040000;
    private const uint WS_SYSMENU = 0x00080000;
    private const uint WS_MAXIMIZEBOX = 0x00010000;
    private const uint WS_MINIMIZEBOX = 0x00020000;
    private const uint WS_EX_CLIENTEDGE = 0x00000200;
    private const uint WS_EX_WINDOWEDGE = 0x00000100;
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_FRAMECHANGED = 0x0020;

    public IntPtr EngineHandle => _engineHwnd;

    public bool IsAttached => _attached;

    /// Raised (physical pixels) when the embedded viewport changes size, so the host can resize
    /// the runtime swapchain to match — otherwise the rendered image stretches.
    public event Action<int, int>? ViewportResized;

    private int _lastW, _lastH;

    // Avalonia sizes/positions whatever handle we return, so we own a real WS_CHILD container
    // window here and reparent the engine window into it. Returning parent.Handle instead would
    // attach the engine to the top-level window and it would not sit inside this panel.
    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        _childHwnd = CreateWindowEx(
            0, "STATIC", string.Empty,
            WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1,
            parent.Handle, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);

        if (_childHwnd == IntPtr.Zero)
            _childHwnd = parent.Handle; // fallback: better than nothing

        // The engine window may have been created before this container existed.
        if (_pendingEngineHwnd != IntPtr.Zero)
        {
            var pending = _pendingEngineHwnd;
            _pendingEngineHwnd = IntPtr.Zero;
            EmbedEngineWindow(pending);
        }

        return new PlatformHandle(_childHwnd, "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        _attached = false;
        if (_childHwnd != IntPtr.Zero)
        {
            DestroyWindow(_childHwnd);
            _childHwnd = IntPtr.Zero;
        }
    }

    public void EmbedEngineWindow(IntPtr engineHwnd)
    {
        if (engineHwnd == IntPtr.Zero)
            return;

        // Container not created yet — remember it and embed from CreateNativeControlCore.
        if (_childHwnd == IntPtr.Zero)
        {
            _pendingEngineHwnd = engineHwnd;
            return;
        }

        _engineHwnd = engineHwnd;

        SetParent(engineHwnd, _childHwnd);

        var style = GetWindowLong(engineHwnd, GWL_STYLE);
        style &= ~(int)(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
        style |= (int)WS_CHILD;
        SetWindowLong(engineHwnd, GWL_STYLE, style);

        var exStyle = GetWindowLong(engineHwnd, GWL_EXSTYLE);
        exStyle &= ~(int)(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE);
        SetWindowLong(engineHwnd, GWL_EXSTYLE, exStyle);

        SetWindowPos(engineHwnd, IntPtr.Zero, 0, 0, 0, 0,
            SWP_NOZORDER | SWP_FRAMECHANGED);

        // Must be set before the first resize — ResizeEngineWindow bails out when not attached.
        _attached = true;
        FitEngineToContainer();
    }

    // Sizes the engine window to the container's real client area (physical pixels), which is
    // DPI-correct — Avalonia's ArrangeOverride size is in logical units.
    private void FitEngineToContainer()
    {
        if (_engineHwnd == IntPtr.Zero || _childHwnd == IntPtr.Zero || !_attached)
            return;
        if (!GetClientRect(_childHwnd, out var rect))
            return;
        ApplySize(rect.Right - rect.Left, rect.Bottom - rect.Top);
    }

    private void ApplySize(int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        ResizeEngineWindow(w, h);
        if (w != _lastW || h != _lastH)
        {
            _lastW = w;
            _lastH = h;
            ViewportResized?.Invoke(w, h);
        }
    }

    public void ResizeEngineWindow(int width, int height)
    {
        if (_engineHwnd == IntPtr.Zero || !_attached || width <= 0 || height <= 0)
            return;

        MoveWindow(_engineHwnd, 0, 0, width, height, true);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        var size = base.ArrangeOverride(finalSize);

        // Avalonia moves the child HWND *after* arrange, so GetClientRect still reports the old
        // size here — that is why maximising left the engine window at its previous dimensions.
        // Size from the layout result immediately, then reconcile against the real client rect on
        // the next dispatcher pass, once the host has repositioned the handle.
        var scale = (VisualRoot as ILayoutRoot)?.LayoutScaling ?? 1.0;
        ApplySize((int)Math.Round(size.Width * scale), (int)Math.Round(size.Height * scale));

        Dispatcher.UIThread.Post(FitEngineToContainer, DispatcherPriority.Loaded);
        return size;
    }
}
