using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using EditorCore;

namespace TucanoEditor.Extensibility;

/// Renders registered inspector sections into a panel and keeps them in sync.
///
/// The main window hands it a container once; from then on, adding a runtime feature to the
/// inspector is `extensions.AddSection(new WhateverSection())` and nothing else.
public sealed class InspectorHost
{
    private readonly Panel _container;
    private readonly EditorExtensions _extensions;
    private readonly Dictionary<IInspectorSection, Expander> _built = new();

    public InspectorHost(Panel container, EditorExtensions extensions)
    {
        _container = container;
        _extensions = extensions;
    }

    /// Shows the sections that apply to `context` and refreshes them.
    /// Sections are built lazily on first use and reused afterwards, so switching selection does
    /// not rebuild control trees.
    public void Update(InspectorContext context)
    {
        foreach (var section in _extensions.Sections)
        {
            var applies = section.AppliesTo(context);

            if (!_built.TryGetValue(section, out var expander))
            {
                if (!applies) continue; // don't build what has never been needed
                expander = BuildExpander(section);
                _built[section] = expander;
                _container.Children.Add(expander);
            }

            expander.IsVisible = applies;
            if (applies) section.Refresh(context);
        }
    }

    private Expander BuildExpander(IInspectorSection section)
    {
        var icon = new Avalonia.Controls.Shapes.Path
        {
            Classes = { "ico" },
            Stroke = new SolidColorBrush(Color.Parse("#FFB300")),
        };
        if (Avalonia.Application.Current?.Resources.TryGetResource(section.IconKey, null, out var geo) == true
            && geo is Geometry g)
        {
            icon.Data = g;
        }

        var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 7 };
        header.Children.Add(icon);
        header.Children.Add(new TextBlock
        {
            Text = section.Title,
            Classes = { "section" },
            VerticalAlignment = VerticalAlignment.Center,
        });

        return new Expander
        {
            Header = header,
            IsExpanded = true,
            Content = section.CreateContent(),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
    }
}
