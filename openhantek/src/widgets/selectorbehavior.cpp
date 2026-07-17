// SPDX-License-Identifier: GPL-2.0-or-later

#include "selectorbehavior.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QWheelEvent>

#include <cstdlib>


namespace {

constexpr int ANGLE_DELTA_PER_STEP = 120;

class SelectorWheelEventFilter final : public QObject {
  public:
    explicit SelectorWheelEventFilter( QComboBox *comboBox ) : QObject( comboBox ) {}

  protected:
    bool eventFilter( QObject *watched, QEvent *event ) override {
        if ( event->type() != QEvent::Wheel )
            return QObject::eventFilter( watched, event );

        auto *comboBox = qobject_cast< QComboBox * >( watched );
        if ( !comboBox || !comboBox->isEnabled() || comboBox->view()->isVisible() )
            return QObject::eventFilter( watched, event );

        auto *wheelEvent = static_cast< QWheelEvent * >( event );
        if ( wheelEvent->phase() == Qt::ScrollBegin )
            accumulatedAngleDelta = 0;

        const int angleDelta = wheelEvent->angleDelta().y();
        if ( angleDelta == 0 ) {
            if ( wheelEvent->phase() == Qt::ScrollEnd )
                accumulatedAngleDelta = 0;
            return QObject::eventFilter( watched, event );
        }

        if ( ( accumulatedAngleDelta > 0 && angleDelta < 0 ) || ( accumulatedAngleDelta < 0 && angleDelta > 0 ) )
            accumulatedAngleDelta = 0;

        // High-resolution wheels and trackpads can provide less than the
        // standard 120 angle units in each event.
        accumulatedAngleDelta += angleDelta;
        const int steps = accumulatedAngleDelta / ANGLE_DELTA_PER_STEP;
        accumulatedAngleDelta %= ANGLE_DELTA_PER_STEP;

        if ( steps ) {
            const int direction = steps > 0 ? -1 : 1;
            for ( int step = 0; step < std::abs( steps ); ++step ) {
                int newIndex = comboBox->currentIndex() + direction;
                while ( newIndex >= 0 && newIndex < comboBox->count() &&
                        !( comboBox->model()->index( newIndex, comboBox->modelColumn(), comboBox->rootModelIndex() ).flags() &
                           Qt::ItemIsEnabled ) )
                    newIndex += direction;

                if ( newIndex < 0 || newIndex >= comboBox->count() )
                    break;

                comboBox->setCurrentIndex( newIndex );
                emit comboBox->activated( newIndex );
            }
        }

        if ( wheelEvent->phase() == Qt::ScrollEnd )
            accumulatedAngleDelta = 0;

        wheelEvent->accept();
        return true;
    }

  private:
    int accumulatedAngleDelta = 0;
};

} // namespace


void enableSelectorWheelBehavior( QComboBox *comboBox ) {
    if ( !comboBox )
        return;

    comboBox->setFocusPolicy( Qt::WheelFocus );
    comboBox->installEventFilter( new SelectorWheelEventFilter( comboBox ) );
}
