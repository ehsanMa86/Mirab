/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapServices.h
 * Purpose: OGC WMS and WMTS map services: capabilities parsing and the "Add map service" dialog.
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
#pragma once

#include "Basemap.h"

#include <QDialog>
#include <QUrl>

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace mirab::app
{

/// A layer offered by a WMS or WMTS server.
struct ServiceLayer
{
    QString name; ///< WMS layer name or WMTS layer identifier
    QString title;
    QStringList styles;
    QStringList crs;                   ///< WMS: coordinate systems the layer can be requested in
    QStringList formats;               ///< WMTS: tile formats
    QStringList matrixSets;            ///< WMTS: linked tile matrix sets
    QHash<QString, QString> templates; ///< WMTS: format -> ResourceURL template
};

struct ServiceMatrixSet
{
    QString id;
    QString crs; ///< normalised, e.g. "EPSG:3857"
    QVector<WmtsMatrix> matrices;
};

struct ServiceCapabilities
{
    QString type; ///< "wms" or "wmts"
    QString version;
    QString title;
    QString getUrl;      ///< GetMap or GetTile address (KVP)
    QStringList formats; ///< WMS GetMap formats
    QVector<ServiceLayer> layers;
    QVector<ServiceMatrixSet> matrixSets;
    QString error; ///< set when the document is not usable
};

/// Parses a WMS 1.1.1 / 1.3.0 or WMTS 1.0.0 GetCapabilities document.
ServiceCapabilities parseCapabilities(const QByteArray& xml);
/// GetCapabilities request for a service address ("wms" or "wmts").
QUrl capabilitiesUrl(const QString& address, const QString& type);
/// "EPSG:3857" for the spellings used by OGC services ("urn:ogc:def:crs:EPSG::3857",
/// "EPSG:900913", ...), "EPSG:4326" for CRS:84; the text unchanged otherwise.
QString normaliseCrs(const QString& text);

/// Fills the WMTS fields of @p p (template and matrices) from capabilities.
bool applyWmtsCapabilities(const ServiceCapabilities& caps, Basemap::Provider& p);

/// Dialog that defines a custom XYZ, WMS or WMTS basemap.
class MapServiceDialog : public QDialog
{
    Q_OBJECT
public:
    MapServiceDialog(const QString& modelCrs, QWidget* parent = nullptr);
    Basemap::Provider provider() const;

private:
    void typeChanged();
    void loadLayers();
    void layerChanged();
    void validate();
    const ServiceLayer* currentLayer() const;

    QString modelCrs_;
    QNetworkAccessManager* network_;
    ServiceCapabilities caps_;
    QComboBox* type_;
    QLineEdit* name_;
    QLineEdit* url_;
    QPushButton* load_;
    QLabel* hint_;
    QComboBox* layer_;
    QComboBox* style_;
    QComboBox* format_;
    QComboBox* crs_;
    QLineEdit* attribution_;
    QLabel* status_;
    QPushButton* ok_;
};

} // namespace mirab::app
