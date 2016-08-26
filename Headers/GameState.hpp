#ifndef GAMESTATE_HPP
#define GAMESTATE_HPP

#include "State.hpp"
#include "World.hpp"
#include "Player.hpp"

#include <SFML/Graphics/Text.hpp>


class GameState : public State
{
public:
	GameState(StateStack& stack, Context context);

	virtual void		draw();
	virtual bool		update(sf::Time dt);
	virtual bool		handleEvent(const sf::Event& event);


private:
	World			mWorld;
	Player&			mPlayer;

	Aircraft*           mAircraft;
	sf::Text            mScoreText;
	sf::Int64           mScore;
	sf::Text			mLevelText;
	bool				mShowText;
	sf::Time			mTextEffectTime;
};

#endif // GAMESTATE_HPP