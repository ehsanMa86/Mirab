/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/AnimationExport.cpp
 * Purpose: Export of the results animation as an animated GIF or a numbered PNG sequence.
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
#include "AnimationExport.h"

#include "MapDecorations.h"
#include "MapView.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace mirab::app
{

namespace gif = core::gif;

std::vector<int> AnimationSettings::frames(int periods) const
{
    std::vector<int> out;
    if (periods <= 0)
        return out;
    const int a = std::clamp(first, 0, periods - 1);
    const int b = last < 0 ? periods - 1 : std::clamp(last, a, periods - 1);
    for (int p = a; p <= b; p += std::max(1, stride))
        out.push_back(p);
    return out;
}

// ---------------------------------------------------------------------------
// Exporter
// ---------------------------------------------------------------------------
AnimationExporter::AnimationExporter(MapView* map, ProjectDocument* doc,
                                     const AnimationSettings& settings, QObject* parent)
    : QObject(parent), map_(map), doc_(doc), s_(settings), timer_(new QTimer(this))
{
    timer_->setInterval(5);
    connect(timer_, &QTimer::timeout, this, &AnimationExporter::step);
}

AnimationExporter::~AnimationExporter()
{
    cancel_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

QString AnimationExporter::framePath(int index) const
{
    const QFileInfo fi(s_.path);
    return fi.dir().filePath(
        QStringLiteral("%1_%2.png").arg(fi.completeBaseName()).arg(index + 1, 4, 10, QChar('0')));
}

void AnimationExporter::start()
{
    const auto* r = doc_->results();
    if (!r || r->periodCount() == 0)
    {
        done(false, tr("Run a simulation first: the animation shows the results over time."));
        return;
    }
    frames_ = s_.frames(r->periodCount());
    size_ = s_.size.isEmpty() ? map_->size() : s_.size;
    size_ = size_.boundedTo(QSize(4096, 4096)).expandedTo(QSize(64, 48));
    base_ = std::clamp(std::min(size_.width(), size_.height()) / 42.0, 10.0, 28.0);
    emit progress(0, frameCount(), tr("Loading basemap tiles..."));
    waitForTiles();
}

void AnimationExporter::waitForTiles()
{
    if (cancel_)
        return;
    MapView::RenderOptions o;
    o.network = false;
    o.basemap = s_.basemap;
    if (s_.basemap && waited_ < 100 &&
        map_->prepareBasemap(QRectF(QPointF(0, 0), QSizeF(size_)), o))
    {
        ++waited_;
        QTimer::singleShot(200, this, &AnimationExporter::waitForTiles);
        return;
    }
    begin();
}

void AnimationExporter::begin()
{
    const QRectF target(QPointF(0, 0), QSizeF(size_));
    // The background (basemap, backdrop) is the same in every frame: draw it once.
    background_ = QImage(size_, QImage::Format_RGB32);
    background_.fill(Qt::white);
    {
        QPainter p(&background_);
        MapView::RenderOptions o;
        o.network = false;
        o.basemap = s_.basemap;
        o.font = decor::font(base_ * 0.85);
        map_->render(p, target, o);
    }
    groundPerUnit_ = map_->groundPerUnit(target, &groundUnit_);

    if (s_.gif)
    {
        // One palette for all frames, from the first, middle and last frames;
        // the legend colours stay exact.
        std::vector<QImage> samples;
        for (std::size_t i : {std::size_t{0}, frames_.size() / 2, frames_.size() - 1})
            samples.push_back(renderFrame(frames_[i]));
        std::vector<gif::PixelView> views;
        for (const QImage& img : samples)
            views.push_back({reinterpret_cast<const std::uint32_t*>(img.constBits()), img.width(),
                             img.height(), static_cast<int>(img.bytesPerLine() / 4)});
        std::vector<gif::Color> keys;
        for (const QColor& c : MapView::rampColors())
            keys.push_back({static_cast<std::uint8_t>(c.red()),
                            static_cast<std::uint8_t>(c.green()),
                            static_cast<std::uint8_t>(c.blue())});
        keys.push_back({255, 255, 255});
        palette_ = gif::animationPalette(views, keys, 255);
    }
    worker_ = std::thread([this] { work(); });
    timer_->start();
}

QImage AnimationExporter::renderFrame(int period)
{
    QImage img = background_.copy();
    QPainter p(&img);
    const QRectF target(QPointF(0, 0), QSizeF(size_));
    MapView::RenderOptions o;
    o.background = false;
    o.period = period;
    o.font = decor::font(base_ * 0.85);
    map_->render(p, target, o);

    const double margin = base_ * 0.8;
    const double w = size_.width(), h = size_.height();
    double left = margin;
    if (!s_.title.isEmpty())
        left = decor::labelBox(p, QPointF(margin, margin), Qt::AlignLeft | Qt::AlignTop, s_.title,
                               decor::font(base_ * 1.15, true))
                   .bottom() +
               margin * 0.6;
    double right = margin;
    if (s_.timeStamp && doc_->results())
    {
        const double t = doc_->results()->times[static_cast<std::size_t>(period)];
        right = decor::labelBox(p, QPointF(w - margin, margin), Qt::AlignRight | Qt::AlignTop,
                                tr("Time %1").arg(QString::fromStdString(text::formatDuration(t))),
                                decor::font(base_ * 1.05, true))
                    .bottom() +
                margin * 0.6;
    }
    if (s_.legend)
        decor::legend(&p, QPointF(margin, left), map_->legend(), {}, decor::font(base_ * 0.85),
                      true, h - left - base_ * 4);
    if (s_.northArrow)
    {
        const double a = base_ * 3.2;
        decor::northArrow(p, QRectF(w - margin - a, right, a, a), map_->northAngle(),
                          decor::font(base_), true);
    }
    if (s_.scaleBar)
        decor::scaleBar(&p, QPointF(margin + base_ * 0.8, h - margin - base_ * 0.5), groundPerUnit_,
                        groundUnit_, w * 0.25, decor::font(base_ * 0.8), true);
    const QString attribution = s_.basemap ? map_->attribution() : QString();
    if (!attribution.isEmpty())
    {
        const QFont f = decor::font(std::max(9.0, base_ * 0.62));
        const QFontMetricsF fm(f);
        const QRectF box(w - fm.horizontalAdvance(attribution) - 10, h - fm.height() - 4,
                         fm.horizontalAdvance(attribution) + 10, fm.height() + 4);
        p.fillRect(box, QColor(255, 255, 255, 210));
        p.setFont(f);
        p.setPen(QColor(60, 70, 85));
        p.drawText(box, Qt::AlignCenter, attribution);
    }
    return img;
}

void AnimationExporter::work()
{
    std::unique_ptr<std::ofstream> out;
    std::unique_ptr<gif::AnimationWriter> writer;
    auto fail = [this](const QString& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = message;
    };
    if (s_.gif)
    {
        out = std::make_unique<std::ofstream>(std::filesystem::u8path(s_.path.toStdString()),
                                              std::ios::binary);
        if (!*out)
        {
            fail(tr("%1 could not be written.").arg(s_.path));
            workerDone_ = true;
            return;
        }
        gif::AnimationWriter::Options opt;
        opt.delayCs = std::max(2, static_cast<int>(std::lround(100.0 / std::max(0.1, s_.fps))));
        opt.dither = s_.dither ? gif::Dither::Ordered : gif::Dither::None;
        writer = std::make_unique<gif::AnimationWriter>(*out, size_.width(), size_.height(),
                                                        palette_, opt);
    }
    for (;;)
    {
        QImage img;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || closed_ || cancel_; });
            if (cancel_ || queue_.empty())
                break;
            img = std::move(queue_.front());
            queue_.pop_front();
        }
        cv_.notify_all();
        if (writer)
        {
            writer->addFrame({reinterpret_cast<const std::uint32_t*>(img.constBits()), img.width(),
                              img.height(), static_cast<int>(img.bytesPerLine() / 4)});
        }
        else if (!img.save(framePath(written_)))
        {
            fail(tr("%1 could not be written.").arg(framePath(written_)));
            break;
        }
        ++written_;
    }
    if (writer && !cancel_ && !writer->finish())
        fail(tr("%1 could not be written.").arg(s_.path));
    workerDone_ = true;
}

void AnimationExporter::step()
{
    if (finished_)
        return;
    std::size_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queued = queue_.size();
    }
    if (next_ < frameCount() && queued < 3)
    {
        QImage frame = renderFrame(frames_[static_cast<std::size_t>(next_)]);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(frame));
            if (++next_ == frameCount())
                closed_ = true;
        }
        cv_.notify_all();
    }
    emit progress(written_, frameCount(),
                  tr("Writing frame %1 of %2...")
                      .arg(std::min<int>(written_ + 1, frameCount()))
                      .arg(frameCount()));
    if (workerDone_)
    {
        worker_.join();
        QString error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error = error_;
        }
        if (!error.isEmpty())
            done(false, error);
        else if (s_.gif)
            done(true,
                 tr("Saved %1 frames to %2").arg(written_).arg(QFileInfo(s_.path).fileName()));
        else
            done(true, tr("Saved %1 frames as %2 ... %3")
                           .arg(written_)
                           .arg(QFileInfo(framePath(0)).fileName(),
                                QFileInfo(framePath(written_ - 1)).fileName()));
    }
}

void AnimationExporter::cancel()
{
    if (finished_)
        return;
    cancel_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    done(false, tr("The animation export was cancelled."));
}

void AnimationExporter::done(bool ok, const QString& message)
{
    timer_->stop();
    finished_ = true;
    if (!ok)
    {
        if (s_.gif)
            QFile::remove(s_.path);
        else
            for (int i = 0; i < written_; ++i)
                QFile::remove(framePath(i));
    }
    emit finished(ok, message);
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------
AnimationDialog::AnimationDialog(MapView* map, ProjectDocument* doc, QWidget* parent)
    : QDialog(parent), map_(map), doc_(doc)
{
    setWindowTitle(tr("Export animation"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Saves the map coloring over the reporting periods as an animated "
                                "GIF or as numbered PNG "
                                "images, with the current view, colors and basemap."),
                             this);
    intro->setWordWrap(true);
    intro->setObjectName("muted");
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    format_ = new QComboBox(this);
    format_->addItem(tr("Animated GIF"));
    format_->addItem(tr("PNG image sequence"));
    form->addRow(tr("Format"), format_);

    from_ = new QComboBox(this);
    to_ = new QComboBox(this);
    const auto* r = doc_->results();
    const int periods = r ? r->periodCount() : 0;
    for (int i = 0; i < periods; ++i)
    {
        const QString label =
            QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(i)]));
        from_->addItem(label);
        to_->addItem(label);
    }
    to_->setCurrentIndex(std::max(0, periods - 1));
    auto* range = new QHBoxLayout;
    range->addWidget(from_, 1);
    range->addWidget(new QLabel(tr("to"), this));
    range->addWidget(to_, 1);
    form->addRow(tr("Periods"), range);

    stride_ = new QSpinBox(this);
    stride_->setRange(1, std::max(1, periods));
    stride_->setPrefix(tr("every "));
    stride_->setSuffix(tr(" period(s)"));
    form->addRow(tr("Use"), stride_);

    fps_ = new QDoubleSpinBox(this);
    fps_->setRange(0.5, 25.0);
    fps_->setSingleStep(0.5);
    fps_->setDecimals(1);
    fps_->setValue(4.0);
    fps_->setSuffix(tr(" frames per second"));
    form->addRow(tr("Speed"), fps_);

    size_ = new QComboBox(this);
    size_->addItem(tr("Map view (%1 x %2)").arg(map_->width()).arg(map_->height()), QSize());
    for (const QSize& s : {QSize(640, 480), QSize(800, 600), QSize(1280, 720), QSize(1920, 1080)})
        size_->addItem(QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()), s);
    size_->addItem(tr("Custom"), QSize(-1, -1));
    width_ = new QSpinBox(this);
    height_ = new QSpinBox(this);
    for (QSpinBox* b : {width_, height_})
    {
        b->setRange(64, 4096);
        b->setSuffix(tr(" px"));
        b->setEnabled(false);
    }
    width_->setValue(map_->width());
    height_->setValue(map_->height());
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(size_, 1);
    sizeRow->addWidget(width_);
    sizeRow->addWidget(new QLabel("x", this));
    sizeRow->addWidget(height_);
    form->addRow(tr("Size"), sizeRow);

    title_ = new QLineEdit(this);
    QString title = QString::fromStdString(doc_->model().title()).trimmed();
    if (title.isEmpty())
        title = QFileInfo(doc_->path()).completeBaseName();
    title_->setText(title);
    title_->setPlaceholderText(tr("No title"));
    form->addRow(tr("Title"), title_);
    layout->addLayout(form);

    auto* show = new QGroupBox(tr("Show"), this);
    auto* grid = new QGridLayout(show);
    QString why;
    basemap_ = new QCheckBox(tr("Basemap"), show);
    basemap_->setChecked(!map_->basemap().isEmpty() && map_->basemapUsable(&why));
    basemap_->setEnabled(basemap_->isChecked());
    legend_ = new QCheckBox(tr("Legend"), show);
    time_ = new QCheckBox(tr("Time stamp"), show);
    scale_ = new QCheckBox(tr("Scale bar"), show);
    north_ = new QCheckBox(tr("North arrow"), show);
    dither_ = new QCheckBox(tr("Smooth colors (dithering)"), show);
    for (QCheckBox* c : {legend_, time_, scale_, north_, dither_})
        c->setChecked(true);
    dither_->setToolTip(
        tr("Mixes palette colors in a fine pattern so basemap imagery keeps its shading. "
           "Flat colors of the network are never dithered."));
    grid->addWidget(basemap_, 0, 0);
    grid->addWidget(legend_, 0, 1);
    grid->addWidget(time_, 0, 2);
    grid->addWidget(scale_, 1, 0);
    grid->addWidget(north_, 1, 1);
    grid->addWidget(dither_, 1, 2);
    layout->addWidget(show);

    summary_ = new QLabel(this);
    summary_->setObjectName("muted");
    layout->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(tr("Export..."));
    // Room for the bold text of the default button.
    ok->setMinimumWidth(static_cast<int>(ok->fontMetrics().horizontalAdvance(ok->text()) * 1.2) +
                        28);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(periods > 0);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(format_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                dither_->setEnabled(format_->currentIndex() == 0);
                updateSummary();
            });
    connect(size_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                const bool custom = size_->currentData().toSize() == QSize(-1, -1);
                width_->setEnabled(custom);
                height_->setEnabled(custom);
            });
    for (QComboBox* c : {from_, to_})
        connect(c, &QComboBox::currentIndexChanged, this, &AnimationDialog::updateSummary);
    connect(stride_, &QSpinBox::valueChanged, this, &AnimationDialog::updateSummary);
    connect(fps_, &QDoubleSpinBox::valueChanged, this, &AnimationDialog::updateSummary);
    updateSummary();
}

AnimationSettings AnimationDialog::settings() const
{
    AnimationSettings s;
    s.gif = format_->currentIndex() == 0;
    s.first = from_->currentIndex();
    s.last = std::max(from_->currentIndex(), to_->currentIndex());
    s.stride = stride_->value();
    s.fps = fps_->value();
    const QSize size = size_->currentData().toSize();
    s.size = size == QSize(-1, -1) ? QSize(width_->value(), height_->value()) : size;
    s.basemap = basemap_->isChecked();
    s.legend = legend_->isChecked();
    s.timeStamp = time_->isChecked();
    s.scaleBar = scale_->isChecked();
    s.northArrow = north_->isChecked();
    s.dither = dither_->isChecked();
    s.title = title_->text().trimmed();
    return s;
}

void AnimationDialog::updateSummary()
{
    const int periods = doc_->results() ? doc_->results()->periodCount() : 0;
    if (periods == 0)
    {
        summary_->setText(tr("Run a simulation first: the animation shows the results over time."));
        return;
    }
    const int frames = static_cast<int>(settings().frames(periods).size());
    summary_->setText(tr("%1 frames, %2 s long")
                          .arg(frames)
                          .arg(frames / std::max(0.1, fps_->value()), 0, 'f', 1));
}

} // namespace mirab::app
