/**
 * @file pulsedots.h
 * @brief Pulse dot animation component
 *
 * Three pulse dots in the center voice status area:
 * - 24x24px fully rounded circles
 * - 1.5s infinite loop pulse effect
 * - Three dots with 0.2s sequential delay
 */

#ifndef PULSEDOTS_H
#define PULSEDOTS_H

#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

class PulseDots : public QWidget
{
    Q_OBJECT

public:
    explicit PulseDots(QWidget *parent = nullptr);

    void startAnimation();
    void stopAnimation();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateDotOpacity(int dot_index, qreal opacity);

    bool is_animating_;
    QTimer *animation_timer_;
    int frame_counter_;

    qreal dot_opacities_[3];
};

#endif // PULSEDOTS_H
