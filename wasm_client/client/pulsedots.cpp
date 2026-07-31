/**
 * @file pulsedots.cpp
 * @brief Pulse dot animation implementation
 *
 * Animation rules:
 * - Opacity cycles between 0.3 ~ 1.0
 * - 1.5s period
 * - ease-in-out easing
 * - Dots delayed by 0.2s each
 */

#include "pulsedots.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

PulseDots::PulseDots(QWidget *parent)
    : QWidget(parent)
    , is_animating_(false)
    , animation_timer_(nullptr)
    , frame_counter_(0)
{
    dot_opacities_[0] = 0.3;
    dot_opacities_[1] = 0.3;
    dot_opacities_[2] = 0.3;

    setFixedSize(104, 32);
    setStyleSheet("background: transparent;");

    animation_timer_ = new QTimer(this);
    animation_timer_->setInterval(30);
    connect(animation_timer_, &QTimer::timeout, this, [this]() {
        frame_counter_++;

        const int frames_per_cycle = 50;

        for (int i = 0; i < 3; ++i) {
            int delay_frames = i * 7;
            int adjusted_frame = (frame_counter_ - delay_frames + frames_per_cycle) % frames_per_cycle;

            qreal phase = static_cast<qreal>(adjusted_frame) / frames_per_cycle * 2.0 * M_PI;
            qreal normalized = (qSin(phase) + 1.0) / 2.0;

            dot_opacities_[i] = 0.3 + normalized * 0.7;
        }

        update();
    });
}

void PulseDots::startAnimation()
{
    is_animating_ = true;
    frame_counter_ = 0;
    animation_timer_->start();
}

void PulseDots::stopAnimation()
{
    is_animating_ = false;
    animation_timer_->stop();
    dot_opacities_[0] = 0.3;
    dot_opacities_[1] = 0.3;
    dot_opacities_[2] = 0.3;
    update();
}

void PulseDots::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int dot_size = 24;
    const int spacing = 8;
    const int total_width = 3 * dot_size + 2 * spacing;
    const int start_x = (width() - total_width) / 2;
    const int start_y = (height() - dot_size) / 2;

    for (int i = 0; i < 3; ++i) {
        QColor dot_color(255, 255, 255);
        dot_color.setAlphaF(dot_opacities_[i]);

        painter.setBrush(dot_color);
        painter.setPen(Qt::NoPen);

        int x = start_x + i * (dot_size + spacing);
        painter.drawEllipse(x, start_y, dot_size, dot_size);
    }
}

void PulseDots::updateDotOpacity(int dot_index, qreal opacity)
{
    if (dot_index >= 0 && dot_index < 3) {
        dot_opacities_[dot_index] = qBound(0.0, opacity, 1.0);
        update();
    }
}
