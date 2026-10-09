/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    plugins/network-statistics/NetworkStatisticsPlugin.cpp
 * Purpose: Example plugin: pipe length by diameter and demand totals, showing how to extend Mirab.
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
#include "mirab/app/PluginInterface.h"
#include "mirab/core/Text.h"

#include <QAction>
#include <QMessageBox>
#include <QObject>
#include <QWidget>

#include <map>

using namespace mirab;

class NetworkStatisticsPlugin : public QObject, public app::MirabPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MirabPlugin_iid)
    Q_INTERFACES(mirab::app::MirabPlugin)

public:
    QString name() const override { return tr("Network statistics"); }
    QString description() const override { return tr("Pipe length by diameter and total base demand."); }
    QString version() const override { return QStringLiteral("1.0"); }

    QList<QAction*> actions(app::PluginHost* host, QObject* parent) override
    {
        auto* a = new QAction(tr("Pipe and demand summary..."), parent);
        connect(a, &QAction::triggered, parent, [host] { showSummary(host); });
        return {a};
    }

private:
    static void showSummary(app::PluginHost* host)
    {
        const core::Model& m = host->model();
        const auto units = m.units();
        QString html;
        if (m.dialect() == core::Dialect::Epanet)
        {
            std::map<double, double> lengthByDiameter;
            double demand = 0.0;
            if (const auto* pipes = m.document().find("PIPES"))
            {
                for (int r = 0; r < pipes->rowCount(); ++r)
                {
                    const auto t = pipes->rowTokens(r);
                    if (t.size() >= 5)
                        lengthByDiameter[text::toDouble(t[4]).value_or(0)] += text::toDouble(t[3]).value_or(0);
                }
            }
            if (const auto* j = m.document().find("JUNCTIONS"))
            {
                for (int r = 0; r < j->rowCount(); ++r)
                {
                    const auto t = j->rowTokens(r);
                    if (t.size() >= 3)
                        demand += text::toDouble(t[2]).value_or(0);
                }
            }
            html += tr("<h3>Pipe length by diameter</h3><table cellpadding=3><tr><th align=left>Diameter (%1)</th>"
                       "<th align=right>Length (%2)</th></tr>")
                        .arg(QString::fromStdString(units.label("diameter")), QString::fromStdString(units.label("length")));
            double total = 0.0;
            for (const auto& [d, len] : lengthByDiameter)
            {
                html += QString("<tr><td>%1</td><td align=right>%2</td></tr>")
                            .arg(QString::fromStdString(text::formatDouble(d, 6)), QString::fromStdString(text::formatDouble(len, 8)));
                total += len;
            }
            html += tr("<tr><td><b>Total</b></td><td align=right><b>%1</b></td></tr></table>")
                        .arg(QString::fromStdString(text::formatDouble(total, 8)));
            html += tr("<p>Total base demand in [JUNCTIONS]: %1 %2</p>")
                        .arg(QString::fromStdString(text::formatDouble(demand, 6)), QString::fromStdString(units.flow));
        }
        else
        {
            double length = 0.0, area = 0.0;
            if (const auto* c = m.document().find("CONDUITS"))
            {
                for (int r = 0; r < c->rowCount(); ++r)
                {
                    const auto t = c->rowTokens(r);
                    if (t.size() >= 4)
                        length += text::toDouble(t[3]).value_or(0);
                }
            }
            if (const auto* s = m.document().find("SUBCATCHMENTS"))
            {
                for (int r = 0; r < s->rowCount(); ++r)
                {
                    const auto t = s->rowTokens(r);
                    if (t.size() >= 4)
                        area += text::toDouble(t[3]).value_or(0);
                }
            }
            html = tr("<p>Total conduit length: %1 %2</p><p>Total subcatchment area: %3 %4</p>")
                       .arg(QString::fromStdString(text::formatDouble(length, 8)), QString::fromStdString(units.label("length")),
                            QString::fromStdString(text::formatDouble(area, 8)), QString::fromStdString(units.label("area")));
        }
        QMessageBox::information(host->window(), tr("Network statistics"), html);
        host->log(tr("Network statistics computed."));
    }
};

#include "NetworkStatisticsPlugin.moc"
