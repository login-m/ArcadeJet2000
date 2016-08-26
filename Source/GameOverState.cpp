#include "GameOverState.hpp"
#include "Utility.hpp"
#include "Player.hpp"
#include "ResourceHolder.hpp"
#include "World.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>
#include <string>




GameOverState::GameOverState(StateStack& stack, Context context)
	: State(stack, context)
	, mGameOverText()
	, mHighScoreText()
	, mPlayerScore()
	, mElapsedTime(sf::Time::Zero)
	, mHighScoreFile()
{
	sf::Font& font = context.fonts->get(Fonts::Arcade);
	sf::Vector2f windowSize(context.window->getSize());

	mGameOverText.setFont(font);
	if (context.player->getMissionStatus() == Player::MissionFailure)
		mGameOverText.setString("Mission failed!");
	else
		mGameOverText.setString("Mission successful!");

	mGameOverText.setCharacterSize(40);
	centerOrigin(mGameOverText);
	mGameOverText.setPosition(0.5f * windowSize.x, 0.4f * windowSize.y);

	mHighScoreText.setFont(font);
	mPlayerScore.setFont(font);
	mHighScoreFile.open("HighScore.txt", std::fstream::in | std::fstream::out | std::fstream::app);

	if (!mHighScoreFile)
		throw std::runtime_error("HighScore.txt could not be loaded.");
	else
	{

		std::string text_score;
		std::getline(mHighScoreFile, text_score);
		long long int currentScore = World::getScore();
		int highScore = toInt(text_score);
		std::string cS = toString(currentScore);


		if (currentScore > highScore)
		{
			mHighScoreFile.close();
			mHighScoreFile.open("HighScore.txt", std::fstream::in | std::fstream::out | std::fstream::trunc);
			if (!mHighScoreFile)
			{
				throw std::runtime_error("High score file could not be loaded.");
			}
			mHighScoreFile << cS;
			mHighScoreFile.close();
		}

		mHighScoreText.setString("Highest score: " + text_score);
		mHighScoreText.setCharacterSize(40);
		centerOrigin(mHighScoreText);
		mHighScoreText.setPosition(0.5f * windowSize.x, 0.8f * windowSize.y);

		text_score = cS;
		mPlayerScore.setString("Current score: " + cS);
		mPlayerScore.setCharacterSize(40);
		centerOrigin(mPlayerScore);
		mPlayerScore.setPosition(0.5f * windowSize.x, 0.9f * windowSize.y);
	}

	World::updateGame();

}

void GameOverState::draw()
{
	sf::RenderWindow& window = *getContext().window;
	window.setView(window.getDefaultView());

	sf::RectangleShape backgroundShape;
	backgroundShape.setFillColor(sf::Color(0, 0, 0, 150));
	backgroundShape.setSize(window.getView().getSize());

	window.draw(backgroundShape);
	window.draw(mGameOverText);
	window.draw(mHighScoreText);
	window.draw(mPlayerScore);
}

bool GameOverState::update(sf::Time dt)
{
	// Show state for 7 seconds, after return to menu
	mElapsedTime += dt;
	if (mElapsedTime > sf::seconds(7))
	{
		requestStateClear();
		requestStackPush(States::Menu);
	}
	return false;
}

bool GameOverState::handleEvent(const sf::Event&)
{
	return false;
}
