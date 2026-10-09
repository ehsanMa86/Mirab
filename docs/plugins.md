# Writing plugins

Plugins add menu actions to Anahita. A plugin is a Qt plugin library that implements `anahita::app::AnahitaPlugin` from [`src/app/include/anahita/app/PluginInterface.h`](../src/app/include/anahita/app/PluginInterface.h).

## What a plugin can do

Through the `PluginHost` it receives, a plugin can:

- read the model (`host->model()`), including every section of the input file,
- change the model with an undoable edit (`host->editModel(...)`),
- read the results of the last run (`host->results()`),
- write to the Messages panel (`host->log(...)`),
- open its own dialogs with `host->window()` as parent.

## Minimal plugin

```cpp
#include "anahita/app/PluginInterface.h"
#include <QAction>
#include <QObject>

class HelloPlugin : public QObject, public anahita::app::AnahitaPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AnahitaPlugin_iid)
    Q_INTERFACES(anahita::app::AnahitaPlugin)
public:
    QString name() const override { return "Hello"; }
    QString description() const override { return "Counts nodes."; }
    QString version() const override { return "1.0"; }
    QList<QAction*> actions(anahita::app::PluginHost* host, QObject* parent) override
    {
        auto* a = new QAction("Count nodes", parent);
        QObject::connect(a, &QAction::triggered, parent, [host] {
            host->log(QString("%1 nodes").arg(host->model().nodes().size()));
        });
        return {a};
    }
};
#include "HelloPlugin.moc"
```

See [`plugins/network-statistics`](../plugins/network-statistics) for a complete example with its CMake file.

## Building and installing

Build the plugin with the same Qt major version and the same compiler family as Anahita, and link it to `Anahita::core` (or compile the core sources into it). Copy the library to one of:

- `plugins/` next to the Anahita executable,
- `../lib/anahita/plugins` on Linux, `Anahita.app/Contents/PlugIns/anahita` on macOS,
- the per-user plugin folder opened by *Tools > Plugins > Open plugin folder*.

Anahita lists loaded plugins in the Messages panel at start-up, and explains why a library was not loaded.
