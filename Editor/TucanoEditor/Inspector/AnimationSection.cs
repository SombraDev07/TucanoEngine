using System;
using System.Collections.Generic;
using Avalonia.Input;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using EditorCore;
using TucanoEditor.Extensibility;

namespace TucanoEditor.Inspector;

/// Playback controls for an object imported with a rig.
///
/// This is the reference implementation of IInspectorSection: it owns its controls, reads the
/// runtime in Refresh, and writes back only from user events. Adding the next runtime feature to
/// the inspector means copying this shape, not editing the main window.
public sealed class AnimationSection : IInspectorSection
{
    public string Title => "ANIMATION";
    public string IconKey => "IcoTransform";
    public int Order => 40; // below Transform and Material, above Physics

    private ComboBox _clips = null!;
    private Button _playPause = null!;
    private Slider _timeline = null!;
    private TextBlock _timeLabel = null!;
    private CheckBox _loop = null!;
    private Slider _speed = null!;
    private TextBlock _info = null!;

    private RuntimeHost? _runtime;
    private uint _object;
    private bool _syncing;

    // An object only gets this section when the runtime says it actually has a skeleton.
    public bool AppliesTo(InspectorContext context) =>
        context.HasObject && context.Runtime.GetBoneCount((uint)context.ObjectIndex) > 0;

    public Control CreateContent()
    {
        _info = new TextBlock { Classes = { "dim" }, TextWrapping = Avalonia.Media.TextWrapping.Wrap };

        _clips = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch };
        _clips.SelectionChanged += (_, _) =>
        {
            if (_syncing || _runtime is null || _clips.SelectedIndex < 0) return;
            _runtime.PlayClip(_object, (uint)_clips.SelectedIndex, _loop.IsChecked == true);
            UpdatePlayPauseLabel();
        };

        _playPause = new Button { Content = "▶", Width = 34, Classes = { "tool" } };
        _playPause.Click += (_, _) =>
        {
            if (_runtime is null) return;
            if (_runtime.IsClipPlaying(_object))
            {
                _runtime.PauseClip(_object, true);
            }
            else if (_runtime.GetCurrentClip(_object) >= 0)
            {
                _runtime.PauseClip(_object, false);
            }
            else if (_clips.SelectedIndex >= 0)
            {
                _runtime.PlayClip(_object, (uint)_clips.SelectedIndex, _loop.IsChecked == true);
            }
            UpdatePlayPauseLabel();
        };

        var stop = new Button { Content = "■", Width = 34, Classes = { "tool" } };
        stop.Click += (_, _) =>
        {
            _runtime?.StopClip(_object);
            UpdatePlayPauseLabel();
        };

        _loop = new CheckBox { Content = "Loop", IsChecked = true };

        _timeline = new Slider { Minimum = 0, Maximum = 1, Value = 0 };
        _timeline.AddHandler(InputElement.PointerPressedEvent, (_, _) => _scrubbing = true,
                             Avalonia.Interactivity.RoutingStrategies.Tunnel);
        _timeline.AddHandler(InputElement.PointerReleasedEvent, (_, _) => _scrubbing = false,
                             Avalonia.Interactivity.RoutingStrategies.Tunnel);
        _timeline.PropertyChanged += (_, e) =>
        {
            if (e.Property != RangeBase.ValueProperty || _syncing || _runtime is null) return;
            // Only write while the user is dragging; otherwise playback's own updates would fight
            // the slider and the clip would stutter.
            if (_scrubbing) _runtime.SetClipTime(_object, (float)_timeline.Value);
        };

        _timeLabel = new TextBlock { Classes = { "dim" }, MinWidth = 76 };

        _speed = new Slider { Minimum = 0, Maximum = 3, Value = 1 };
        _speed.PropertyChanged += (_, e) =>
        {
            if (e.Property != RangeBase.ValueProperty || _syncing || _runtime is null) return;
            _runtime.SetClipSpeed(_object, (float)_speed.Value);
        };

        var transport = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        transport.Children.Add(_playPause);
        transport.Children.Add(stop);
        transport.Children.Add(_loop);

        var speedRow = new Grid { ColumnDefinitions = new ColumnDefinitions("54,*") };
        var speedLabel = new TextBlock { Text = "Speed", Classes = { "dim" }, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(speedLabel, 0);
        Grid.SetColumn(_speed, 1);
        speedRow.Children.Add(speedLabel);
        speedRow.Children.Add(_speed);

        var panel = new StackPanel { Spacing = 4 };
        panel.Children.Add(_info);
        panel.Children.Add(new TextBlock { Text = "Clip", Classes = { "label" } });
        panel.Children.Add(_clips);
        panel.Children.Add(transport);
        panel.Children.Add(new TextBlock { Text = "Time", Classes = { "label" } });
        panel.Children.Add(_timeline);
        panel.Children.Add(_timeLabel);
        panel.Children.Add(speedRow);
        return panel;
    }

    private bool _scrubbing;

    public void Refresh(InspectorContext context)
    {
        if (!context.HasObject) return;
        _runtime = context.Runtime;
        _object = (uint)context.ObjectIndex;

        _syncing = true;
        try
        {
            var clipCount = _runtime.GetClipCount(_object);
            var bones = _runtime.GetBoneCount(_object);
            _info.Text = clipCount == 0
                ? $"{bones} bones, no clips in this file"
                : $"{bones} bones · {clipCount} clip{(clipCount == 1 ? "" : "s")}";

            // Rebuild the clip list only when it actually changed — otherwise every refresh would
            // reset the user's selection.
            if (_clips.ItemCount != (int)clipCount)
            {
                var names = new List<string>();
                for (uint i = 0; i < clipCount; ++i) names.Add(_runtime.GetClipName(_object, i));
                _clips.ItemsSource = names;
            }

            var current = _runtime.GetCurrentClip(_object);
            if (current >= 0 && current < (int)clipCount) _clips.SelectedIndex = current;
            else if (_clips.SelectedIndex < 0 && clipCount > 0) _clips.SelectedIndex = 0;

            var duration = _clips.SelectedIndex >= 0
                ? _runtime.GetClipDuration(_object, (uint)_clips.SelectedIndex)
                : 0f;
            _timeline.Maximum = Math.Max(duration, 0.0001);

            var time = _runtime.GetClipTime(_object);
            if (!_scrubbing) _timeline.Value = Math.Clamp(time, 0, _timeline.Maximum);
            _timeLabel.Text = $"{time:F2}s / {duration:F2}s";
            _speed.Value = Math.Clamp(_runtime.GetClipSpeed(_object), 0, 3);

            UpdatePlayPauseLabel();
        }
        finally
        {
            _syncing = false;
        }
    }

    private void UpdatePlayPauseLabel()
    {
        if (_runtime is null) return;
        _playPause.Content = _runtime.IsClipPlaying(_object) ? "❚❚" : "▶";
    }
}
