#include "GameState.hpp"
#include "MusicPlayer.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include "Utility.hpp"


GameState::GameState(StateStack& stack, Context context)
: State(stack, context)
, mWorld(*context.window, *context.fonts, *context.sounds)
, mPlayer(*context.player)
, mScoreText()
, mScore(0)
, mLevelText()
, mShowText(true)
, mTextEffectTime(sf::Time::Zero)
{
	mPlayer.setMissionStatus(Player::MissionRunning);

	mLevelText.setFont(context.fonts->get(Fonts::Arcade));
	mLevelText.setString("Level "+ (toString(mWorld.getLevel()-1)));
	centerOrigin(mLevelText);
	mLevelText.setPosition(sf::Vector2f(context.window->getSize() / 2u));

	mScoreText.setFont(context.fonts->get(Fonts::Arcade));
	mScoreText.setPosition(700.f, 725.f);
	mScoreText.setCharacterSize(20u);

	// Play game theme
	int level = World::getLevel()-1;
	if(level==1) context.music->play(Music::Level_1);
	else if(level==2) context.music->play(Music::Level_2);
	else if(level==3) context.music->play(Music::Level_3);
	if (level == 4) context.music->play(Music::Level_4);
}

void GameState::draw()
{
	mWorld.draw();


	//Make window part of the class to avoid useless calling everytime
	sf::RenderWindow& window = *getContext().window;
	window.draw(mScoreText);
	if (mShowText)
		window.draw(mLevelText);
}

bool GameState::update(sf::Time dt)
{
	mWorld.update(dt);
	mScoreText.setString("Score: " + toString(World::getScore()));

	mTextEffectTime += dt;

	if (mTextEffectTime >= sf::seconds(4.5f))
	{
		mShowText = false;
	}


	if (!mWorld.hasAlivePlayer())
	{
		mPlayer.setMissionStatus(Player::MissionFailure);
		requestStackPop();
		requestStackPush(States::GameOver);
	}
	else if (mWorld.hasPlayerReachedEnd())
	{
		mPlayer.setMissionStatus(Player::MissionSuccess);
		requestStackPop();
		World::increaseScore();
		if (World::getLevel()==5)
			requestStackPush(States::GameOver);
		else
			requestStackPush(States::Game);
	}

	CommandQueue& commands = mWorld.getCommandQueue();
	mPlayer.handleRealtimeInput(commands);

	return true;
}

bool GameState::handleEvent(const sf::Event& event)
{
	// Game input handling
	CommandQueue& commands = mWorld.getCommandQueue();
	mPlayer.handleEvent(event, commands);

	// Escape pressed, trigger the pause screen
	if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)
		requestStackPush(States::Pause);

	return true;
}