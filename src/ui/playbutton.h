#ifndef PLAYBUTTON_H
#define PLAYBUTTON_H

#include <QPushButton>

class PlayButton : public QPushButton
{
public:
	explicit PlayButton(QWidget* parent = nullptr);
private:
	QString play_text;
	QString pause_text;
};

#endif // PLAYBUTTON_H
