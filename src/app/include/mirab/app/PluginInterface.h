/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/include/mirab/app/PluginInterface.h
 * Purpose: Public C++ interface for Mirab add-on plugins.
 *
 * Copyright (C) 2026 Ehsan Madadi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
//
// A plugin is a Qt plugin (a shared library built with the same Qt major
// version) that implements MirabPlugin. Place it in the "plugins" folder
// next to the Mirab executable, or in the per-user plugin folder shown in
// Tools > Plugins. Mirab calls actions() once at start-up and adds the
// returned actions to the Plugins menu.
#pragma once

#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <QList>
#include <QString>
#include <QtPlugin>

#include <functional>
#include <string>

class QAction;
class QWidget;

namespace mirab::app
{

/// Services the application offers to plugins.
class PluginHost
{
public:
    virtual ~PluginHost() = default;
    virtual const core::Model& model() const = 0;
    /// Applies an undoable edit. Return false from @p fn to abandon it.
    virtual bool editModel(const QString& description,
                           const std::function<bool(core::Model&, std::string*)>& fn) = 0;
    /// Results of the last simulation, or nullptr.
    virtual const core::ResultSet* results() const = 0;
    virtual QString projectPath() const = 0;
    virtual void log(const QString& message) = 0;
    virtual QWidget* window() = 0;
};

class MirabPlugin
{
public:
    virtual ~MirabPlugin() = default;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    /// Creates the plugin's menu actions. The host keeps ownership of @p host.
    virtual QList<QAction*> actions(PluginHost* host, QObject* parent) = 0;
};

} // namespace mirab::app

#define MirabPlugin_iid "org.mirab.MirabPlugin/1.0"
Q_DECLARE_INTERFACE(mirab::app::MirabPlugin, MirabPlugin_iid)
