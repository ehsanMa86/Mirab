# Writing plugins

Plugins add menu actions to Mirab. A plugin is a Qt plugin library that implements `mirab::app::MirabPlugin` from [`src/app/include/mirab/app/PluginInterface.h`](../src/app/include/mirab/app/PluginInterface.h).

## What a plugin can do

Through the `PluginHost` it receives, a plugin can:

- read the model (`host->model()`), including every section of the input file,
- change the model with an undoable edit (`host->editModel(...)`),
- read the results of the last run (`host->results()`),
- write to the Messages panel (`host->log(...)`),
- open its own dialogs with `host->window()` as parent.

## Minimal plugin

```cpp
#include "mirab/app/PluginInterface.h"
#include <QAction>
#include <QObject>

class HelloPlugin : public QObject, public mirab::app::MirabPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MirabPlugin_iid)
    Q_INTERFACES(mirab::app::MirabPlugin)
public:
    QString name() const override { return "Hello"; }
    QString description() const override { return "Counts nodes."; }
    QString version() const override { return "1.0"; }
    QList<QAction*> actions(mirab::app::PluginHost* host, QObject* parent) override
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

Build the plugin with the same Qt major version and the same compiler family as Mirab, and link it to `Mirab::core` (or compile the core sources into it). Copy the library to one of:

- `plugins/` next to the Mirab executable,
- `../lib/mirab/plugins` on Linux, `Mirab.app/Contents/PlugIns/mirab` on macOS,
- the per-user plugin folder opened by *Tools > Plugins > Open plugin folder*.

Mirab lists loaded plugins in the Messages panel at start-up, and explains why a library was not loaded.
