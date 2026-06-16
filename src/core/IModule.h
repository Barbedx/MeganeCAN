#pragma once

// Uniform feature lifecycle. The App composition root holds a fixed list of IModule*,
// calls begin() once (in a deterministic, explicit order — the research warns static
// priority schemes get fragile, so keep the order short and explicit), then service()
// each loop. Features get their ports by constructor injection; adding a feature = write
// a module + register it in App, never scatter edits through main.
struct IModule {
    virtual ~IModule() = default;
    virtual void begin() {}
    virtual void service() {}
    virtual const char* name() const { return "module"; }
};
