using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace TucanoEditor.Converters;

/// Resolves an icon key ("IcoCube") into the geometry declared in Styles/Icons.axaml, so data
/// models can name an icon without referencing UI types.
public sealed class IconConverter : IValueConverter
{
    public static readonly IconConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not string key || string.IsNullOrEmpty(key)) return null;
        if (Application.Current is null) return null;
        return Application.Current.Resources.TryGetResource(key, null, out var geometry)
            ? geometry as Geometry
            : null;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
