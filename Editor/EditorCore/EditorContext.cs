namespace EditorCore;

public sealed class EditorContext
{
    public RuntimeHost Runtime { get; }
    public CommandQueue Commands { get; }
    public EventBus Events { get; }

    public EditorContext(RuntimeHost runtime, CommandQueue commands, EventBus events)
    {
        Runtime = runtime;
        Commands = commands;
        Events = events;
    }
}
