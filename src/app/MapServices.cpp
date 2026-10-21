/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapServices.cpp
 * Purpose: OGC WMS and WMTS capabilities parsing and the dialog that adds a map service.
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
#include "MapServices.h"

#include "mirab/MirabConfig.h"
#include "mirab/core/Crs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace mirab::app
{

QString normaliseCrs(const QString& text)
{
    const QString t = text.trimmed();
    if (t.compare("CRS:84", Qt::CaseInsensitive) == 0 || t.endsWith(":CRS84", Qt::CaseInsensitive))
        return QStringLiteral("EPSG:4326");
    static const QRegularExpression code(QStringLiteral("EPSG[:/](?:[0-9.]*:)?(\\d+)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    const auto m = code.match(t);
    if (m.hasMatch())
    {
        const QString n = m.captured(1);
        return QStringLiteral("EPSG:%1").arg(
            n == "900913" || n == "102100" || n == "102113" ? "3857" : n);
    }
    return t;
}

QUrl capabilitiesUrl(const QString& address, const QString& type)
{
    QUrl url = QUrl::fromUserInput(address.trimmed());
    QUrlQuery q(url);
    bool hasRequest = false;
    for (const auto& item : q.queryItems())
        hasRequest = hasRequest || item.first.compare("request", Qt::CaseInsensitive) == 0;
    if (hasRequest || url.path().endsWith(".xml", Qt::CaseInsensitive))
        return url; // already a capabilities address
    q.addQueryItem("SERVICE", type.toUpper());
    q.addQueryItem("REQUEST", "GetCapabilities");
    url.setQuery(q);
    return url;
}

ServiceCapabilities parseCapabilities(const QByteArray& xml)
{
    ServiceCapabilities caps;
    QXmlStreamReader r(xml);
    QStringList path; // local names of the open elements

    struct LayerContext
    {
        ServiceLayer layer;
    };
    QVector<LayerContext> layers; // WMS layers nest and inherit systems and styles
    ServiceLayer wmtsLayer;
    ServiceMatrixSet set;
    WmtsMatrix matrix;
    QString styleId;
    bool styleDefault = false;
    QString rawSetCrs;
    QString operation, getHref;
    bool getKvp = false;

    auto parent = [&](int up)
    { return path.size() > up ? path.at(path.size() - 1 - up) : QString(); };
    while (!r.atEnd())
    {
        const auto token = r.readNext();
        if (token == QXmlStreamReader::StartElement)
        {
            const QString name = r.name().toString();
            path << name;
            if (path.size() == 1)
            {
                if (name == "WMS_Capabilities" || name == "WMT_MS_Capabilities")
                    caps.type = "wms";
                else if (name == "Capabilities")
                    caps.type = "wmts";
                else if (name.contains("Exception"))
                    caps.error = QObject::tr("The server answered with an error.");
                else
                    caps.error = QObject::tr("This is not a WMS or WMTS capabilities document.");
                caps.version = r.attributes().value("version").toString();
                continue;
            }
            if (caps.type == "wms")
            {
                if (name == "Layer")
                {
                    LayerContext c;
                    if (!layers.isEmpty())
                    {
                        c.layer.crs = layers.last().layer.crs;
                        c.layer.styles = layers.last().layer.styles;
                    }
                    layers << c;
                }
                else if (name == "OnlineResource" && path.contains("GetMap") &&
                         caps.getUrl.isEmpty())
                {
                    caps.getUrl =
                        r.attributes().value("http://www.w3.org/1999/xlink", "href").toString();
                }
            }
            else if (caps.type == "wmts")
            {
                if (name == "Layer" && parent(1) == "Contents")
                {
                    wmtsLayer = ServiceLayer();
                }
                else if (name == "TileMatrixSet" && parent(1) == "Contents")
                {
                    set = ServiceMatrixSet();
                    rawSetCrs.clear();
                }
                else if (name == "TileMatrix" && parent(1) == "TileMatrixSet")
                {
                    matrix = WmtsMatrix();
                }
                else if (name == "Style" && parent(1) == "Layer")
                {
                    styleId.clear();
                    styleDefault = r.attributes().value("isDefault") == QLatin1String("true");
                }
                else if (name == "ResourceURL" && parent(1) == "Layer" &&
                         r.attributes().value("resourceType") == QLatin1String("tile"))
                {
                    wmtsLayer.templates.insert(r.attributes().value("format").toString(),
                                               r.attributes().value("template").toString());
                }
                else if (name == "Operation")
                {
                    operation = r.attributes().value("name").toString();
                }
                else if (name == "Get" && operation == "GetTile")
                {
                    getHref =
                        r.attributes().value("http://www.w3.org/1999/xlink", "href").toString();
                    getKvp = true;
                }
                else if (name == "Constraint" && parent(1) == "Get")
                {
                    getKvp = false; // until a KVP value is listed
                }
            }
        }
        else if (token == QXmlStreamReader::Characters && !r.isWhitespace() && !path.isEmpty())
        {
            const QString text = r.text().toString().trimmed();
            const QString name = path.last();
            const QString up = parent(1);
            if (caps.type == "wms")
            {
                if (name == "Title" && up == "Service")
                    caps.title = text;
                else if (name == "Format" && up == "GetMap")
                    caps.formats << text;
                else if (!layers.isEmpty() && up == "Layer")
                {
                    ServiceLayer& l = layers.last().layer;
                    if (name == "Name")
                        l.name = text;
                    else if (name == "Title")
                        l.title = text;
                    else if (name == "CRS" || name == "SRS")
                    {
                        for (const QString& c : text.split(' ', Qt::SkipEmptyParts))
                        {
                            if (!l.crs.contains(c))
                                l.crs << c;
                        }
                    }
                }
                else if (!layers.isEmpty() && name == "Name" && up == "Style")
                {
                    ServiceLayer& l = layers.last().layer;
                    if (!l.styles.contains(text))
                        l.styles << text;
                }
            }
            else if (caps.type == "wmts")
            {
                if (name == "Title" && up == "ServiceIdentification")
                    caps.title = text;
                else if (up == "Layer" && parent(2) == "Contents")
                {
                    if (name == "Identifier")
                        wmtsLayer.name = text;
                    else if (name == "Title")
                        wmtsLayer.title = text;
                    else if (name == "Format")
                        wmtsLayer.formats << text;
                }
                else if (name == "Identifier" && up == "Style")
                    styleId = text;
                else if (name == "TileMatrixSet" && up == "TileMatrixSetLink")
                    wmtsLayer.matrixSets << text;
                else if (up == "TileMatrixSet" && parent(2) == "Contents")
                {
                    if (name == "Identifier")
                        set.id = text;
                    else if (name == "SupportedCRS")
                    {
                        rawSetCrs = text;
                        set.crs = normaliseCrs(text);
                    }
                }
                else if (up == "TileMatrix")
                {
                    if (name == "Identifier")
                        matrix.id = text;
                    else if (name == "ScaleDenominator")
                        matrix.scaleDenominator = text.toDouble();
                    else if (name == "TopLeftCorner")
                    {
                        const QStringList xy = text.split(' ', Qt::SkipEmptyParts);
                        if (xy.size() >= 2)
                        {
                            matrix.left = xy[0].toDouble();
                            matrix.top = xy[1].toDouble();
                        }
                    }
                    else if (name == "TileWidth")
                        matrix.tileWidth = text.toInt();
                    else if (name == "TileHeight")
                        matrix.tileHeight = text.toInt();
                    else if (name == "MatrixWidth")
                        matrix.matrixWidth = text.toInt();
                    else if (name == "MatrixHeight")
                        matrix.matrixHeight = text.toInt();
                }
                else if (name == "Value" && path.contains("Get") &&
                         text.compare("KVP", Qt::CaseInsensitive) == 0)
                {
                    getKvp = true;
                }
            }
        }
        else if (token == QXmlStreamReader::EndElement && !path.isEmpty())
        {
            const QString name = path.takeLast();
            if (caps.type == "wms" && name == "Layer" && !layers.isEmpty())
            {
                const ServiceLayer l = layers.takeLast().layer;
                if (!l.name.isEmpty())
                    caps.layers << l;
            }
            else if (caps.type == "wmts")
            {
                if (name == "Layer" && parent(0) == "Contents")
                    caps.layers << wmtsLayer;
                else if (name == "Style" && parent(0) == "Layer" && !styleId.isEmpty())
                {
                    if (styleDefault)
                        wmtsLayer.styles.prepend(styleId);
                    else
                        wmtsLayer.styles << styleId;
                }
                else if (name == "TileMatrix" && parent(0) == "TileMatrixSet")
                    set.matrices << matrix;
                else if (name == "TileMatrixSet" && parent(0) == "Contents")
                {
                    // EPSG:4326 corners are written latitude first (except for CRS84).
                    if (set.crs == "EPSG:4326" && !rawSetCrs.contains("CRS84", Qt::CaseInsensitive))
                    {
                        for (auto& m : set.matrices)
                            std::swap(m.left, m.top);
                    }
                    caps.matrixSets << set;
                }
                else if (name == "Get" && operation == "GetTile" && getKvp && caps.getUrl.isEmpty())
                {
                    caps.getUrl = getHref;
                }
            }
        }
    }
    if (r.hasError() && caps.error.isEmpty())
        caps.error =
            QObject::tr("The capabilities document could not be read: %1").arg(r.errorString());
    if (caps.error.isEmpty() && caps.layers.isEmpty())
        caps.error = QObject::tr("The service offers no layers.");
    return caps;
}

bool applyWmtsCapabilities(const ServiceCapabilities& caps, Basemap::Provider& p)
{
    for (const auto& set : caps.matrixSets)
    {
        if (set.id != p.matrixSet)
            continue;
        p.matrices = set.matrices;
        if (p.crs.isEmpty())
            p.crs = set.crs;
    }
    for (const auto& l : caps.layers)
    {
        if (l.name != p.layer)
            continue;
        p.tileTemplate = l.templates.value(p.format);
        if (p.tileTemplate.isEmpty() && !l.templates.isEmpty())
            p.tileTemplate = l.templates.begin().value();
    }
    if (!caps.getUrl.isEmpty() && p.tileTemplate.isEmpty())
        p.url = caps.getUrl;
    return !p.matrices.isEmpty();
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------
MapServiceDialog::MapServiceDialog(const QString& modelCrs, QWidget* parent)
    : QDialog(parent), modelCrs_(normaliseCrs(modelCrs)), network_(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Add map service"));
    resize(600, 420);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Use a web map service as the basemap: a tile address template, an OGC WMS or "
           "an OGC WMTS. Tiles are requested in Web Mercator when the server offers it, "
           "else in the project's coordinate system."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    type_ = new QComboBox(this);
    type_->addItem(tr("Tile address template (XYZ)"), "xyz");
    type_->addItem(tr("WMS"), "wms");
    type_->addItem(tr("WMTS"), "wmts");
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(tr("Name shown in the basemap menu"));
    url_ = new QLineEdit(this);
    load_ = new QPushButton(tr("Load layers"), this);
    auto* urlRow = new QHBoxLayout;
    urlRow->addWidget(url_, 1);
    urlRow->addWidget(load_);
    hint_ = new QLabel(this);
    hint_->setObjectName("muted");
    hint_->setWordWrap(true);
    layer_ = new QComboBox(this);
    style_ = new QComboBox(this);
    format_ = new QComboBox(this);
    crs_ = new QComboBox(this);
    attribution_ = new QLineEdit(this);
    attribution_->setPlaceholderText(tr("Credit text drawn on the map"));
    form->addRow(tr("Type"), type_);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Address"), urlRow);
    form->addRow(QString(), hint_);
    form->addRow(tr("Layer"), layer_);
    form->addRow(tr("Style"), style_);
    form->addRow(tr("Format"), format_);
    form->addRow(tr("Coordinate system"), crs_);
    form->addRow(tr("Attribution"), attribution_);
    layout->addLayout(form);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Add"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(type_, &QComboBox::currentIndexChanged, this, &MapServiceDialog::typeChanged);
    connect(load_, &QPushButton::clicked, this, &MapServiceDialog::loadLayers);
    connect(url_, &QLineEdit::returnPressed, this, &MapServiceDialog::loadLayers);
    connect(layer_, &QComboBox::currentIndexChanged, this, &MapServiceDialog::layerChanged);
    for (QLineEdit* e : {name_, url_})
        connect(e, &QLineEdit::textChanged, this, &MapServiceDialog::validate);
    for (QComboBox* c : {layer_, crs_})
        connect(c, &QComboBox::currentIndexChanged, this, &MapServiceDialog::validate);
    typeChanged();
}

void MapServiceDialog::typeChanged()
{
    const QString t = type_->currentData().toString();
    const bool ogc = t != "xyz";
    load_->setVisible(ogc);
    for (QWidget* w : {static_cast<QWidget*>(layer_), static_cast<QWidget*>(style_),
                       static_cast<QWidget*>(format_), static_cast<QWidget*>(crs_)})
        w->setEnabled(ogc);
    if (t == "xyz")
    {
        url_->setPlaceholderText("https://tiles.example.org/{z}/{x}/{y}.png");
        hint_->setText(
            tr("Use {z}, {x} and {y} for the tile; {-y} counts rows from the bottom (TMS) and {s} "
               "picks a subdomain a, b or c. The tiles must be in Web Mercator."));
    }
    else
    {
        url_->setPlaceholderText(t == "wms" ? "https://maps.example.org/wms"
                                            : "https://maps.example.org/wmts");
        hint_->setText(
            tr("Enter the service address and press Load layers to read its capabilities."));
    }
    caps_ = ServiceCapabilities();
    layer_->clear();
    style_->clear();
    format_->clear();
    crs_->clear();
    validate();
}

void MapServiceDialog::loadLayers()
{
    const QString type = type_->currentData().toString();
    if (type == "xyz" || url_->text().trimmed().isEmpty())
        return;
    QNetworkRequest req(capabilitiesUrl(url_->text(), type));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mirab/" MIRAB_VERSION_STRING " (+" MIRAB_HOMEPAGE ")"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    status_->setText(tr("Reading the capabilities of the service..."));
    load_->setEnabled(false);
    QNetworkReply* reply = network_->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, type]
            {
                reply->deleteLater();
                load_->setEnabled(true);
                if (reply->error() != QNetworkReply::NoError)
                {
                    status_->setText(
                        tr("The service could not be reached: %1").arg(reply->errorString()));
                    return;
                }
                caps_ = parseCapabilities(reply->readAll());
                if (caps_.error.isEmpty() && caps_.type != type)
                    caps_.error =
                        tr("The address answered as a %1 service.").arg(caps_.type.toUpper());
                layer_->clear();
                if (!caps_.error.isEmpty())
                {
                    status_->setText(caps_.error);
                    validate();
                    return;
                }
                for (const auto& l : caps_.layers)
                    layer_->addItem(l.title.isEmpty() || l.title == l.name
                                        ? l.name
                                        : l.title + "  (" + l.name + ")",
                                    l.name);
                if (name_->text().trimmed().isEmpty())
                    name_->setText(caps_.title);
                status_->setText(
                    tr("%n layer(s) found.", nullptr, static_cast<int>(caps_.layers.size())));
                layerChanged();
            });
}

const ServiceLayer* MapServiceDialog::currentLayer() const
{
    const QString name = layer_->currentData().toString();
    for (const auto& l : caps_.layers)
    {
        if (l.name == name)
            return &l;
    }
    return nullptr;
}

void MapServiceDialog::layerChanged()
{
    style_->clear();
    format_->clear();
    crs_->clear();
    const ServiceLayer* l = currentLayer();
    if (!l)
    {
        validate();
        return;
    }
    style_->addItems(l->styles.isEmpty() ? QStringList{QString()} : l->styles);
    auto usable = [](const QString& c)
    { return crs::parse(normaliseCrs(c).toStdString()).has_value(); };
    auto rank = [this](const QString& c)
    {
        const QString n = normaliseCrs(c);
        return n == "EPSG:3857" ? 0 : n == modelCrs_ ? 1 : n == "EPSG:4326" ? 2 : 3;
    };
    if (caps_.type == "wms")
    {
        QStringList formats = caps_.formats;
        formats.sort();
        for (const QString& f : {QStringLiteral("image/jpeg"), QStringLiteral("image/png")})
        {
            if (formats.removeAll(f))
                formats.prepend(f);
        }
        format_->addItems(formats.isEmpty() ? QStringList{"image/png"} : formats);
        QStringList systems;
        for (const QString& c : l->crs)
        {
            if (usable(c))
                systems << c;
        }
        std::stable_sort(systems.begin(), systems.end(),
                         [&](const QString& a, const QString& b) { return rank(a) < rank(b); });
        for (const QString& c : systems)
            crs_->addItem(c, c);
    }
    else
    {
        format_->addItems(l->formats.isEmpty() ? QStringList{"image/png"} : l->formats);
        QVector<const ServiceMatrixSet*> sets;
        for (const auto& s : caps_.matrixSets)
        {
            if (l->matrixSets.contains(s.id) && usable(s.crs) && !s.matrices.isEmpty())
                sets << &s;
        }
        std::stable_sort(sets.begin(), sets.end(),
                         [&](const ServiceMatrixSet* a, const ServiceMatrixSet* b)
                         { return rank(a->crs) < rank(b->crs); });
        for (const auto* s : sets)
            crs_->addItem(QStringLiteral("%1  (%2)").arg(s->id, s->crs), s->id);
    }
    validate();
}

void MapServiceDialog::validate()
{
    const QString t = type_->currentData().toString();
    QString problem;
    if (name_->text().trimmed().isEmpty())
        problem = tr("Enter a name.");
    if (t == "xyz")
    {
        const QString u = url_->text();
        if (!u.contains("{z}") || !u.contains("{x}") || !(u.contains("{y}") || u.contains("{-y}")))
            problem = tr("The address must contain {z}, {x} and {y}.");
    }
    else if (layer_->count() == 0)
        problem = tr("Load the layers of the service.");
    else if (crs_->count() == 0)
        problem = tr("The layer is not offered in a coordinate system Mirab can draw.");
    ok_->setEnabled(problem.isEmpty());
    if (!problem.isEmpty() && (t == "xyz" || layer_->count() > 0))
        status_->setText(problem);
    else if (problem.isEmpty() && t != "xyz")
        status_->setText(tr("Ready: %1 in %2.").arg(layer_->currentText(), crs_->currentText()));
}

Basemap::Provider MapServiceDialog::provider() const
{
    Basemap::Provider p;
    p.custom = true;
    p.label = name_->text().trimmed();
    p.id = "custom:" + p.label;
    p.type = type_->currentData().toString();
    p.attribution = attribution_->text().trimmed();
    p.url = url_->text().trimmed();
    if (p.type == "xyz")
    {
        if (p.url.contains("{s}"))
            p.subdomains = {"a", "b", "c"};
        return p;
    }
    p.layer = layer_->currentData().toString();
    p.style = style_->currentText();
    p.format = format_->currentText();
    p.version = caps_.version;
    if (p.type == "wms")
    {
        p.crs = crs_->currentData().toString();
        if (!caps_.getUrl.isEmpty())
            p.url = caps_.getUrl;
    }
    else
    {
        p.matrixSet = crs_->currentData().toString();
        applyWmtsCapabilities(caps_, p);
    }
    return p;
}

} // namespace mirab::app
