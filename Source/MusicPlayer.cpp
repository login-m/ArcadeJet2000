#include "MusicPlayer.hpp"


MusicPlayer::MusicPlayer()
: mMusic()
, mFilenames()
, mVolume(100.f)
{
	mFilenames[Music::Welcome] = "Media/Music/Welcome.ogg";
	mFilenames[Music::MenuTheme]    = "Media/Music/MenuTheme.ogg";
	mFilenames[Music::Level_1] = "Media/Music/level1.ogg";
	mFilenames[Music::Level_2] = "Media/Music/level2.ogg";
	mFilenames[Music::Level_3] = "Media/Music/level3.ogg";
	mFilenames[Music::Level_4] = "Media/Music/level4.ogg";
}

void MusicPlayer::play(Music::ID theme)
{
	std::string filename = mFilenames[theme];

	if (!mMusic.openFromFile(filename))
		throw std::runtime_error("Music " + filename + " could not be loaded.");

	mMusic.setVolume(mVolume);
	mMusic.setLoop(true);
	mMusic.play();
}

void MusicPlayer::stop()
{
	mMusic.stop();
}

void MusicPlayer::setVolume(float volume)
{
	mVolume = volume;
}

void MusicPlayer::setPaused(bool paused)
{
	if (paused)
		mMusic.pause();
	else
		mMusic.play();
}
