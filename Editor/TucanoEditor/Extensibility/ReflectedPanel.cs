using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using EditorCore;
using EditorCore.Interop;

namespace TucanoEditor.Extensibility;

/// Editing UI built entirely from what the engine says its parameter blocks contain.
///
/// There is deliberately no per-field code anywhere in this file — no mention of water, fog, or any
/// field name. The engine declares a field; this builds a control for it. That is the whole point:
/// the hand-written inspector cost UI code per property, which is why panels drifted out of sync
/// with the structs they edited and why adding a parameter was expensive.
///
/// Adding a field to a reflected struct makes it appear here on the next run with no change to the
/// editor at all.
public sealed class ReflectedPanel : IEditorPanel
{
    public string Id => "reflected-params";
    public string Title => "Renderer Parameters";
    public string IconKey => "IcoTag";

    public Window CreateWindow(RuntimeHost runtime) => new ReflectedParamsWindow(runtime);

    public void OnSceneChanged(Window window, RuntimeHost runtime)
    {
        if (window is ReflectedParamsWindow w) w.ReloadFromRuntime();
    }
}

/// Window hosting one generated section per reflected block.
public sealed class ReflectedParamsWindow : Window
{
    private readonly RuntimeHost _runtime;
    private readonly List<Action> _refreshers = new();

    // Writing a control's value while refreshing must not echo back as a user edit.
    private bool _loading;

    private const double LabelWidth = 158;

    public ReflectedParamsWindow(RuntimeHost runtime)
    {
        _runtime = runtime;

        Title = "Renderer Parameters";
        Width = 460;
        Height = 820;
        MinWidth = 400;
        MinHeight = 420;
        Background = Brush.Parse("#0B0B0D");
        Foreground = Brush.Parse("#E8E8EC");
        FontFamily = new FontFamily("Segoe UI");

        Content = BuildContent();
    }

    /// Re-reads every value from the engine. Cheap: it is a scalar read per component.
    public void ReloadFromRuntime()
    {
        if (_runtime is not { IsAlive: true }) return;
        _loading = true;
        try
        {
            foreach (var refresh in _refreshers) refresh();
        }
        finally
        {
            _loading = false;
        }
    }

    private Control BuildContent()
    {
        var stack = new StackPanel { Spacing = 8, Margin = new Avalonia.Thickness(10) };

        if (_runtime is not { IsAlive: true })
        {
            stack.Children.Add(new TextBlock { Text = "Runtime not available." });
            return new ScrollViewer { Content = stack };
        }

        var header = new TextBlock
        {
            Text = "Generated from the engine's field declarations — no per-field editor code.",
            FontSize = 11.5,
            Foreground = Brush.Parse("#6B6B75"),
            TextWrapping = TextWrapping.Wrap,
            Margin = new Avalonia.Thickness(2, 0, 2, 4),
        };
        stack.Children.Add(header);

        var blockCount = _runtime.ReflectedBlockCount;
        for (uint b = 0; b < blockCount; ++b)
        {
            stack.Children.Add(BuildBlock(b));
        }

        if (blockCount == 0)
        {
            stack.Children.Add(new TextBlock
            {
                Text = "The engine exposes no reflected parameter blocks.",
                Foreground = Brush.Parse("#9A9AA4"),
            });
        }

        ReloadFromRuntime();
        return new ScrollViewer { Content = stack };
    }

    private Control BuildBlock(uint block)
    {
        var inner = new StackPanel { Spacing = 2 };

        inner.Children.Add(new TextBlock
        {
            Text = _runtime.ReflectedBlockName(block).ToUpperInvariant(),
            FontSize = 11,
            FontWeight = FontWeight.SemiBold,
            Foreground = Brush.Parse("#FFC94A"),
            Margin = new Avalonia.Thickness(0, 0, 0, 6),
        });

        foreach (var (field, index) in Enumerate(_runtime.ReflectedFields(block)))
        {
            inner.Children.Add(BuildField(block, index, field));
        }

        return new Border
        {
            Background = Brush.Parse("#191920"),
            BorderBrush = Brush.Parse("#3A2C10"),
            BorderThickness = new Avalonia.Thickness(1),
            CornerRadius = new Avalonia.CornerRadius(4),
            Padding = new Avalonia.Thickness(12),
            Child = inner,
        };
    }

    private static IEnumerable<(RuntimeHost.ReflectedField, uint)> Enumerate(
        List<RuntimeHost.ReflectedField> fields)
    {
        for (uint i = 0; i < fields.Count; ++i) yield return (fields[(int)i], i);
    }

    private Control BuildField(uint block, uint index, RuntimeHost.ReflectedField field)
    {
        return field.Type switch
        {
            TucanoFieldType.Bool => BuildBool(block, index, field),
            TucanoFieldType.Color => BuildComponents(block, index, field, new[] { "R", "G", "B" }, showSwatch: true),
            TucanoFieldType.Vec2 => BuildComponents(block, index, field, new[] { "X", "Y" }, showSwatch: false),
            TucanoFieldType.Vec3 => BuildComponents(block, index, field, new[] { "X", "Y", "Z" }, showSwatch: false),
            _ => BuildScalar(block, index, field),
        };
    }

    private Control BuildBool(uint block, uint index, RuntimeHost.ReflectedField field)
    {
        var box = new CheckBox
        {
            Content = field.Label,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Avalonia.Thickness(0, 3, 0, 3),
        };
        if (!string.IsNullOrEmpty(field.Tooltip)) ToolTip.SetTip(box, field.Tooltip);

        box.IsCheckedChanged += (_, _) =>
        {
            if (_loading) return;
            _runtime.ReflectedSet(block, index, box.IsChecked == true ? 1f : 0f);
        };
        _refreshers.Add(() => box.IsChecked = _runtime.ReflectedGet(block, index) != 0f);
        return box;
    }

    private Control BuildScalar(uint block, uint index, RuntimeHost.ReflectedField field)
    {
        var readout = MakeReadout();
        var slider = MakeSlider(field);

        slider.PropertyChanged += (_, e) =>
        {
            if (e.Property != RangeBase.ValueProperty) return;
            readout.Text = FormatValue(slider.Value, field);
            if (_loading) return;
            _runtime.ReflectedSet(block, index, (float)slider.Value);
        };
        _refreshers.Add(() =>
        {
            var v = _runtime.ReflectedGet(block, index);
            slider.Value = Math.Clamp(v, field.MinValue, field.MaxValue);
            readout.Text = FormatValue(v, field);
        });

        return Row(field, new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,Auto"),
            Children = { Place(slider, 0), Place(readout, 1) },
        });
    }

    private Control BuildComponents(uint block, uint index, RuntimeHost.ReflectedField field,
                                    string[] axes, bool showSwatch)
    {
        var rows = new StackPanel { Spacing = 2 };
        Border? swatch = null;

        if (showSwatch)
        {
            swatch = new Border
            {
                Width = 34,
                Height = 16,
                CornerRadius = new Avalonia.CornerRadius(3),
                BorderBrush = Brush.Parse("#3A3A45"),
                BorderThickness = new Avalonia.Thickness(1),
                Background = Brushes.White,
                VerticalAlignment = VerticalAlignment.Center,
            };
        }

        var sliders = new Slider[Math.Min(axes.Length, (int)field.Components)];

        void UpdateSwatch()
        {
            if (swatch is null) return;
            // Values can exceed 1 (tints are multipliers), so normalise for display only.
            float Get(int i) => i < sliders.Length ? (float)sliders[i].Value : 0f;
            var max = Math.Max(1f, Math.Max(Get(0), Math.Max(Get(1), Get(2))));
            swatch.Background = new SolidColorBrush(Color.FromRgb(
                (byte)(Math.Clamp(Get(0) / max, 0f, 1f) * 255),
                (byte)(Math.Clamp(Get(1) / max, 0f, 1f) * 255),
                (byte)(Math.Clamp(Get(2) / max, 0f, 1f) * 255)));
        }

        for (var c = 0; c < sliders.Length; ++c)
        {
            var component = (uint)c;
            var readout = MakeReadout();
            var slider = MakeSlider(field);
            sliders[c] = slider;

            slider.PropertyChanged += (_, e) =>
            {
                if (e.Property != RangeBase.ValueProperty) return;
                readout.Text = FormatValue(slider.Value, field);
                UpdateSwatch();
                if (_loading) return;
                _runtime.ReflectedSet(block, index, (float)slider.Value, component);
            };
            _refreshers.Add(() =>
            {
                var v = _runtime.ReflectedGet(block, index, component);
                slider.Value = Math.Clamp(v, field.MinValue, field.MaxValue);
                readout.Text = FormatValue(v, field);
                UpdateSwatch();
            });

            var axis = new TextBlock
            {
                Text = axes[c],
                Width = 14,
                FontSize = 11,
                Foreground = AxisBrush(c),
                VerticalAlignment = VerticalAlignment.Center,
            };

            rows.Children.Add(new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"),
                Children = { Place(axis, 0), Place(slider, 1), Place(readout, 2) },
            });
        }

        Control right = rows;
        if (swatch is not null)
        {
            right = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto,*"),
                Children = { Place(swatch, 0), Place(rows, 1) },
            };
        }

        return Row(field, right);
    }

    // ── Small builders ──

    private static IBrush AxisBrush(int component) => component switch
    {
        0 => Brush.Parse("#D94F2B"),
        1 => Brush.Parse("#5FBF66"),
        _ => Brush.Parse("#4E8FC4"),
    };

    private static TextBlock MakeReadout() => new()
    {
        Width = 52,
        FontSize = 11,
        Foreground = Brush.Parse("#9A9AA4"),
        HorizontalAlignment = HorizontalAlignment.Right,
        TextAlignment = TextAlignment.Right,
        VerticalAlignment = VerticalAlignment.Center,
        Margin = new Avalonia.Thickness(6, 0, 0, 0),
    };

    private static Slider MakeSlider(RuntimeHost.ReflectedField field)
    {
        var span = Math.Max(field.MaxValue - field.MinValue, 1e-6f);
        var step = field.Step > 0f ? field.Step : span / 100f;
        return new Slider
        {
            Minimum = field.MinValue,
            Maximum = field.MaxValue,
            SmallChange = step,
            LargeChange = step * 10,
            // Integer-valued fields snap; continuous ones stay smooth.
            IsSnapToTickEnabled = field.Type == TucanoFieldType.Int,
            TickFrequency = step,
            VerticalAlignment = VerticalAlignment.Center,
        };
    }

    private static string FormatValue(double v, RuntimeHost.ReflectedField field) =>
        field.Type == TucanoFieldType.Int ? ((int)Math.Round(v)).ToString()
                                          : v.ToString(Math.Abs(v) >= 100 ? "F0" : "F3");

    private static Control Row(RuntimeHost.ReflectedField field, Control editor)
    {
        var label = new TextBlock
        {
            Text = field.Label,
            Width = LabelWidth,
            FontSize = 11.5,
            Foreground = Brush.Parse("#9A9AA4"),
            TextWrapping = TextWrapping.Wrap,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Avalonia.Thickness(0, 0, 8, 0),
        };
        if (!string.IsNullOrEmpty(field.Tooltip)) ToolTip.SetTip(label, field.Tooltip);

        return new Grid
        {
            ColumnDefinitions = new ColumnDefinitions($"{LabelWidth},*"),
            Margin = new Avalonia.Thickness(0, 3, 0, 3),
            Children = { Place(label, 0), Place(editor, 1) },
        };
    }

    private static Control Place(Control c, int column)
    {
        Grid.SetColumn(c, column);
        return c;
    }
}
