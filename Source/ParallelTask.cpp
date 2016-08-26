#include "ParallelTask.hpp"

ParallelTask::ParallelTask()
	: mThread(&ParallelTask::runTask, this)
	, mFinished(false)
{
}

void ParallelTask::execute()
{
	mFinished = false;
	mElapsedTime.restart();
	mThread.launch();
}

bool ParallelTask::isFinished()
{
	sf::Lock lock(mMutex);
	return mFinished;
}

float ParallelTask::getCompletion()
{
	sf::Lock lock(mMutex);

	//100% at 10 seconds of elapsed time
	return mElapsedTime.getElapsedTime().asSeconds() / 10.f;
}


//By locking the sf::Mutex object before touching sensitive data, we ensure that if
//a thread tries to lock an already locked resource, it will wait until the other thread
//unlocks it.This creates synchronization between threads because they will wait in
//line to access shared data one at a time.
//Because sf::Lock is a RAII compliant class, as soon as it goes out of scope and is
//destructed, the sf::Mutex object automatically unlocks.
void ParallelTask::runTask()
{
	// Dummy task - stall 10 seconds
	bool ended = false;
	while (!ended)
	{
		sf::Lock lock(mMutex); // protect the clock
		if (mElapsedTime.getElapsedTime().asSeconds() >= 10.f)
			ended = true;
	}

	{ // mFinished may be accessed from multiple threads, protect it
		sf::Lock lock(mMutex);
		mFinished = true;
	}
}