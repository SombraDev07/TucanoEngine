using System.Collections.Generic;
using System.Threading.Tasks;

namespace EditorCore;

public class PanelDescriptor
{
    public string Title { get; set; } = "";
    public string Id { get; set; } = "";
    public object? Content { get; set; }
}

public interface ITucanoPlugin
{
    string Name { get; }
    string Version { get; }
    Task InitializeAsync(EditorContext ctx);
    IEnumerable<PanelDescriptor> GetPanels();
    Task ShutdownAsync();
}
