#include "World.hpp"
#include "Projectile.hpp"
#include "Pickup.hpp"
#include "Foreach.hpp"
#include "TextNode.hpp"
#include "ParticleNode.hpp"
#include "SoundNode.hpp"
#include <SFML/Graphics/RenderTarget.hpp>


#include <algorithm>
#include <cmath>
#include <limits>


World::World(sf::RenderTarget& outputTarget, FontHolder& fonts, SoundPlayer& sounds)
	: mTarget(outputTarget)
	, mSceneTexture()
	, mWorldView(outputTarget.getDefaultView())
	, mTextures()
	, mFonts(fonts)
	, mSounds(sounds)
	, mSceneGraph()
	, mSceneLayers()
	, mWorldBounds(0.f, 0.f, mWorldView.getSize().x, 10000.f)
	, mSpawnPosition(mWorldView.getSize().x / 2.f, mWorldBounds.height - mWorldView.getSize().y / 2.f)
	, mScrollSpeed(mLevel == 1 ? -100.f : (mLevel == 2 ? -125.f : (mLevel == 3 ? -125.f : (mLevel == 4 ? -150.f : -150.f))))
	, mPlayerAircraft(nullptr)
	, mEnemySpawnPoints()
	, mActiveEnemies()
{
	mSceneTexture.create(mTarget.getSize().x, mTarget.getSize().y);


	loadTextures();
	buildScene();

	// Prepare the view
	mWorldView.setCenter(mSpawnPosition);
}

int World::mLevel = 1;
long long int World::mScore = 0;

void World::update(sf::Time dt)
{
	// Scroll the world, reset player velocity
	mWorldView.move(0.f, mScrollSpeed * dt.asSeconds());
	mPlayerAircraft->setVelocity(0.f, 0.f);
	
	// Setup commands to destroy entities, and guide missiles
	destroyEntitiesOutsideView();
	guideMissiles();

	// Forward commands to scene graph, adapt velocity (scrolling, diagonal correction)
	while (!mCommandQueue.isEmpty())
		mSceneGraph.onCommand(mCommandQueue.pop(), dt);
	adaptPlayerVelocity();

	// Collision detection and response (may destroy entities)
	handleCollisions();

	// Remove all destroyed entities, create new ones
	mSceneGraph.removeWrecks();
	spawnEnemies();

	// Regular update step, adapt position (correct if outside view)
	mSceneGraph.update(dt, mCommandQueue);
	adaptPlayerPosition();


	updateSounds();
}

void World::draw()
{


	if (PostEffect::isSupported())
	{

		mSceneTexture.clear();
		mSceneTexture.setView(mWorldView);
		mSceneTexture.draw(mSceneGraph);
		mSceneTexture.display();
		mBloomEffect.apply(mSceneTexture, mTarget);
	}
	else
	{
		mTarget.setView(mWorldView);
		mTarget.draw(mSceneGraph);
	}
}

CommandQueue& World::getCommandQueue()
{
	return mCommandQueue;
}

bool World::hasAlivePlayer() const
{
	return !mPlayerAircraft->isMarkedForRemoval();
}

bool World::hasPlayerReachedEnd() const
{
	return !mWorldBounds.contains(mPlayerAircraft->getPosition());
}

void World::loadTextures()
{
	mTextures.load(Textures::Entities, "Media/Textures/Entities.png");
	mTextures.load(Textures::Jungle, "Media/Textures/Jungle.png");
	mTextures.load(Textures::Space3, "Media/Textures/Space3.png");
	mTextures.load(Textures::Space2, "Media/Textures/Space2.png");
	mTextures.load(Textures::Space1, "Media/Textures/Space1.png");
	mTextures.load(Textures::Explosion, "Media/Textures/Explosion.png");
	mTextures.load(Textures::Particle, "Media/Textures/Particle.png");
	mTextures.load(Textures::FinishLine, "Media/Textures/FinishLine.png");
}

void World::adaptPlayerPosition()
{
	// Keep player's position inside the screen bounds, at least borderDistance units from the border
	sf::FloatRect viewBounds = getViewBounds();
	const float borderDistance = 40.f;

	sf::Vector2f position = mPlayerAircraft->getPosition();
	position.x = std::max(position.x, viewBounds.left + borderDistance);
	position.x = std::min(position.x, viewBounds.left + viewBounds.width - borderDistance);
	position.y = std::max(position.y, viewBounds.top + borderDistance);
	position.y = std::min(position.y, viewBounds.top + viewBounds.height - borderDistance);
	mPlayerAircraft->setPosition(position);
}

void World::adaptPlayerVelocity()
{
	sf::Vector2f velocity = mPlayerAircraft->getVelocity();

	// If moving diagonally, reduce velocity (to have always same velocity)
	if (velocity.x != 0.f && velocity.y != 0.f)
		mPlayerAircraft->setVelocity(velocity / std::sqrt(2.f));

	// Add scrolling velocity
	mPlayerAircraft->accelerate(0.f, mScrollSpeed);
}

bool matchesCategories(SceneNode::Pair& colliders, Category::Type type1, Category::Type type2)
{
	unsigned int category1 = colliders.first->getCategory();
	unsigned int category2 = colliders.second->getCategory();

	// Make sure first pair entry has category type1 and second has type2
	if (type1 & category1 && type2 & category2)
	{
		return true;
	}
	else if (type1 & category2 && type2 & category1)
	{
		std::swap(colliders.first, colliders.second);
		return true;
	}
	else
	{
		return false;
	}
}

void World::handleCollisions()
{
	std::set<SceneNode::Pair> collisionPairs;
	mSceneGraph.checkSceneCollision(mSceneGraph, collisionPairs);

	FOREACH(SceneNode::Pair pair, collisionPairs)
	{
		if (matchesCategories(pair, Category::PlayerAircraft, Category::EnemyAircraft))
		{
			auto& player = static_cast<Aircraft&>(*pair.first);
			auto& enemy = static_cast<Aircraft&>(*pair.second);

			// Collision: Player damage = enemy's remaining HP
			player.damage(enemy.getHitpoints());
			enemy.destroy();
			if (enemy.getType() == Aircraft::Avenger) mScore += 50;
			else if (enemy.getType() == Aircraft::Raptor) mScore += 10;
			else if (enemy.getType() == Aircraft::C83) mScore+=100;
		}

		else if (matchesCategories(pair, Category::PlayerAircraft, Category::Pickup))
		{
			auto& player = static_cast<Aircraft&>(*pair.first);
			auto& pickup = static_cast<Pickup&>(*pair.second);

			// Apply pickup effect to player, destroy projectile
			pickup.apply(player);
			pickup.destroy();
			player.playLocalSound(mCommandQueue, SoundEffect::CollectPickup);
		}

		else if (matchesCategories(pair, Category::EnemyAircraft, Category::AlliedProjectile)
			|| matchesCategories(pair, Category::PlayerAircraft, Category::EnemyProjectile))
		{
			auto& aircraft = static_cast<Aircraft&>(*pair.first);
			auto& projectile = static_cast<Projectile&>(*pair.second);

			// Apply projectile damage to aircraft, destroy projectile
			aircraft.damage(projectile.getDamage());
			projectile.destroy();
			if (aircraft.isDestroyed())
			{
				if (aircraft.getType() == Aircraft::Avenger) mScore += 50;
				else if (aircraft.getType() == Aircraft::Raptor) mScore += 10;
			}
		}
	}
}

void World::updateSounds()
{
	// Set listener's position to player position
	mSounds.setListenerPosition(mPlayerAircraft->getWorldPosition());

	// Remove unused sounds
	mSounds.removeStoppedSounds();
}

void World::buildScene()
{
	// Initialize the different layers
	for (std::size_t i = 0; i < LayerCount; ++i)
	{
		Category::Type category = (i == LowerAir) ? Category::SceneAirLayer : Category::None;

		SceneNode::Ptr layer(new SceneNode(category));
		mSceneLayers[i] = layer.get();

		mSceneGraph.attachChild(std::move(layer));
	}

	float viewHeight = mWorldView.getSize().y;
	sf::IntRect textureRect(mWorldBounds);
	textureRect.height += static_cast<int>(viewHeight);

	// Add enemy aircrafts
	addEnemies();

	// Prepare the tiled background	
	sf::Texture& chosenTexture = mTextures.get(mLevel == 1 ? Textures::Jungle : (mLevel == 2 ? Textures::Space1 : (mLevel == 3 ? Textures::Space2 : Textures::Space3)));
	chosenTexture.setRepeated(true);
	++mLevel;



	// Add the background sprite to the scene
	std::unique_ptr<SpriteNode> chosenSprite(new SpriteNode(chosenTexture, textureRect));
	chosenSprite->setPosition(mWorldBounds.left, mWorldBounds.top - viewHeight);
	mSceneLayers[Background]->attachChild(std::move(chosenSprite));

	// Add the finish line to the scene
	sf::Texture& finishTexture = mTextures.get(Textures::FinishLine);
	std::unique_ptr<SpriteNode> finishSprite(new SpriteNode(finishTexture));
	finishSprite->setPosition(0.f, -76.f);
	mSceneLayers[Background]->attachChild(std::move(finishSprite));

	// Add particle node to the scene
	std::unique_ptr<ParticleNode> smokeNode(new ParticleNode(Particle::Smoke, mTextures));
	mSceneLayers[LowerAir]->attachChild(std::move(smokeNode));

	// Add propellant particle node to the scene
	std::unique_ptr<ParticleNode> propellantNode(new ParticleNode(Particle::Propellant, mTextures));
	mSceneLayers[LowerAir]->attachChild(std::move(propellantNode));

	// Add sound effect node
	std::unique_ptr<SoundNode> soundNode(new SoundNode(mSounds));
	mSceneGraph.attachChild(std::move(soundNode));

	// Add player's aircraft
	std::unique_ptr<Aircraft> player(new Aircraft(Aircraft::Eagle, mTextures, mFonts));
	mPlayerAircraft = player.get();
	mPlayerAircraft->setPosition(mSpawnPosition);
	mSceneLayers[UpperAir]->attachChild(std::move(player));

}

void World::addEnemies()
{
	// Add enemies to the spawn point container

	// * * *   LEVEL 1   * * * 
	if (mLevel == 1) {
		addEnemy(Aircraft::Raptor, 0.f, 1350.f);
		addEnemy(Aircraft::Raptor, +100.f, 1350.f);
		addEnemy(Aircraft::Raptor, -100.f, 1350.f);

		addEnemy(Aircraft::Avenger, 100.f, 1500.f);
		addEnemy(Aircraft::Avenger, -100.f, 1500.f);
		addEnemy(Aircraft::Avenger, -240.f, 1500.f);
		addEnemy(Aircraft::Avenger, 240.f, 1500.f);
		addEnemy(Aircraft::Raptor, 150.f, 1700.f);
		addEnemy(Aircraft::Raptor, -150.f, 1700.f);
		addEnemy(Aircraft::Avenger, 300.f, 1850.f);
		addEnemy(Aircraft::Avenger, -300.f, 1850.f);
		addEnemy(Aircraft::Avenger, 420.f, 2000.f);
		addEnemy(Aircraft::Avenger, -420.f, 2000.f);

		addEnemy(Aircraft::Raptor, 50.f, 2150.f);
		addEnemy(Aircraft::Raptor, -50.f, 2150.f);
		addEnemy(Aircraft::Raptor, 300.f, 2200.f);
		addEnemy(Aircraft::Raptor, -300.f, 2200.f);
		addEnemy(Aircraft::Raptor, 125.f, 2350.f);
		addEnemy(Aircraft::Raptor, -125.f, 2350.f);
		addEnemy(Aircraft::Avenger, 200.f, 2700.f);
		addEnemy(Aircraft::Avenger, -200.f, 2700.f);
		addEnemy(Aircraft::Avenger, 420.f, 2700.f);
		addEnemy(Aircraft::Avenger, -420.f, 2700.f);

		addEnemy(Aircraft::Raptor, 0.f, 3450.f);
		addEnemy(Aircraft::Raptor, 100.f, 3450.f);
		addEnemy(Aircraft::Raptor, -100.f, 3450.f);
		addEnemy(Aircraft::Raptor, 250.f, 3450.f);
		addEnemy(Aircraft::Raptor, -250.f, 3450.f);
		addEnemy(Aircraft::Avenger, 0.f, 3650.f);
		addEnemy(Aircraft::Avenger, 75.f, 3650.f);
		addEnemy(Aircraft::Avenger, -75.f, 3650.f);
		addEnemy(Aircraft::Avenger, 420.f, 3650.f);
		addEnemy(Aircraft::Avenger, -420.f, 3650.f);

		addEnemy(Aircraft::Avenger, 0.f, 4000.f);
		addEnemy(Aircraft::Avenger, 300.f, 4000.f);
		addEnemy(Aircraft::Avenger, -300.f, 4000.f);
		addEnemy(Aircraft::Raptor, 200.f, 4250.f);
		addEnemy(Aircraft::Raptor, -200.f, 4250.f);
		addEnemy(Aircraft::Avenger, 0.f, 4800.f);
		addEnemy(Aircraft::Avenger, 150.f, 4800.f);
		addEnemy(Aircraft::Avenger, -150.f, 4800.f);
		addEnemy(Aircraft::Avenger, 420.f, 4500.f);
		addEnemy(Aircraft::Avenger, -420.f, 4500.f);


		addEnemy(Aircraft::Avenger, 0.f, 5000.f);
		addEnemy(Aircraft::Avenger, 250.f, 5250.f);
		addEnemy(Aircraft::Avenger, -250.f, 5250.f);
		addEnemy(Aircraft::Raptor, 0.f, 5350.f);
		addEnemy(Aircraft::Raptor, 0.f, 5450.f);
		addEnemy(Aircraft::Raptor, 0.f, 5550.f);
		addEnemy(Aircraft::Avenger, -200.f, 5600.f);
		addEnemy(Aircraft::Avenger, 200.f, 5600.f);
		addEnemy(Aircraft::Avenger, 420.f, 5600.f);
		addEnemy(Aircraft::Avenger, -420.f, 5600.f);


		addEnemy(Aircraft::Avenger, 0.f, 6000.f);
		addEnemy(Aircraft::Raptor, 150.f, 6000.f);
		addEnemy(Aircraft::Raptor, -150.f, 6000.f);
		addEnemy(Aircraft::Avenger, 300.f, 6000.f);
		addEnemy(Aircraft::Avenger, -300.f, 6000.f);
		addEnemy(Aircraft::Avenger, 100.f, 6200.f);
		addEnemy(Aircraft::Avenger, -100.f, 6200.f);
		addEnemy(Aircraft::Avenger, 0.f, 6200.f);
		addEnemy(Aircraft::Avenger, 420.f, 6200.f);
		addEnemy(Aircraft::Avenger, -420.f, 6200.f);

		addEnemy(Aircraft::Raptor, -75.f, 6450.f);
		addEnemy(Aircraft::Raptor, 75.f, 6450.f);
		addEnemy(Aircraft::Raptor, 300.f, 6450.f);
		addEnemy(Aircraft::Raptor, -300.f, 6450.f);

		addEnemy(Aircraft::Avenger, -200.f, 6650.f);
		addEnemy(Aircraft::Avenger, 200.f, 6650.f);
		addEnemy(Aircraft::Avenger, 0.f, 6700.f);
		addEnemy(Aircraft::Avenger, 100.f, 6750.f);
		addEnemy(Aircraft::Avenger, -100.f, 6750.f);

		addEnemy(Aircraft::Avenger, 420.f, 7000.f);
		addEnemy(Aircraft::Avenger, -420.f, 7000.f);
		addEnemy(Aircraft::Raptor, 0.f, 7050.f);
		addEnemy(Aircraft::Raptor, +100.f, 7350.f);
		addEnemy(Aircraft::Raptor, -100.f, 7350.f);
		addEnemy(Aircraft::Avenger, 100.f, 7500.f);
		addEnemy(Aircraft::Avenger, -100.f, 7500.f);
		addEnemy(Aircraft::Avenger, -240.f, 7500.f);
		addEnemy(Aircraft::Avenger, 240.f, 7500.f);
		addEnemy(Aircraft::Raptor, 150.f, 7700.f);
		addEnemy(Aircraft::Raptor, -150.f, 7700.f);
		addEnemy(Aircraft::Avenger, 300.f, 7850.f);
		addEnemy(Aircraft::Avenger, -300.f, 7850.f);


		addEnemy(Aircraft::Raptor, 125.f, 8150.f);
		addEnemy(Aircraft::Raptor, 230.f, 8150.f);
		addEnemy(Aircraft::Raptor, -125.f, 8150.f);
		addEnemy(Aircraft::Raptor, -230.f, 8150.f);
		addEnemy(Aircraft::Raptor, 0.f, 8500.f);

		addEnemy(Aircraft::Raptor, 125.f, 9150.f);
		addEnemy(Aircraft::Raptor, 170.f, 9150.f);
		addEnemy(Aircraft::Raptor, -125.f, 9150.f);
		addEnemy(Aircraft::Raptor, -170.f, 9150.f);
		addEnemy(Aircraft::Raptor, 50.f, 9300.f);
		addEnemy(Aircraft::Raptor, -50.f, 9300.f);
		addEnemy(Aircraft::Raptor, 350.f, 9600.f);
		addEnemy(Aircraft::Raptor, -350.f, 9600.f);

	}
	else if (mLevel == 2)
	{

		addEnemy(Aircraft::Raptor, 0.f, 9750.f);
		addEnemy(Aircraft::Raptor, +100.f, 9750.f);
		addEnemy(Aircraft::Raptor, -100.f, 9750.f);
		addEnemy(Aircraft::Avenger, 100.f, 9500.f);
		addEnemy(Aircraft::Avenger, -100.f, 9500.f);
		addEnemy(Aircraft::Avenger, -240.f, 9500.f);
		addEnemy(Aircraft::Avenger, 240.f, 9500.f);
		addEnemy(Aircraft::Raptor, 150.f, 9300.f);
		addEnemy(Aircraft::Raptor, -150.f, 9300.f);
		addEnemy(Aircraft::Avenger, 300.f, 9250.f);
		addEnemy(Aircraft::Avenger, -300.f, 9250.f);
		addEnemy(Aircraft::Avenger, 420.f, 9250.f);
		addEnemy(Aircraft::Avenger, -420.f, 9250.f);

		addEnemy(Aircraft::Raptor, 50.f, 8950.f);
		addEnemy(Aircraft::Raptor, -50.f, 8950.f);
		addEnemy(Aircraft::Raptor, 300.f, 8800.f);
		addEnemy(Aircraft::Raptor, -300.f, 8800.f);
		addEnemy(Aircraft::Raptor, 125.f, 8750.f);
		addEnemy(Aircraft::Raptor, -125.f, 8450.f);
		addEnemy(Aircraft::Avenger, 200.f, 8300.f);
		addEnemy(Aircraft::Avenger, -200.f, 8300.f);
		addEnemy(Aircraft::Avenger, 420.f, 8400.f);
		addEnemy(Aircraft::Avenger, -420.f, 8400.f);

		addEnemy(Aircraft::Raptor, 0.f, 7950.f);
		addEnemy(Aircraft::Raptor, 100.f, 7650.f);
		addEnemy(Aircraft::Raptor, -100.f, 7650.f);
		addEnemy(Aircraft::Raptor, 250.f, 7450.f);
		addEnemy(Aircraft::Raptor, -250.f, 7450.f);
		addEnemy(Aircraft::Avenger, 0.f, 7350.f);
		addEnemy(Aircraft::Avenger, 75.f, 7050.f);
		addEnemy(Aircraft::Avenger, -75.f, 7050.f);
		addEnemy(Aircraft::Avenger, 420.f, 7300.f);
		addEnemy(Aircraft::Avenger, -420.f, 7300.f);

		addEnemy(Aircraft::Avenger, 0.f, 6800.f);
		addEnemy(Aircraft::Avenger, 300.f, 6800.f);
		addEnemy(Aircraft::Avenger, -300.f, 6800.f);
		addEnemy(Aircraft::Raptor, 200.f, 6550.f);
		addEnemy(Aircraft::Raptor, -200.f, 6550.f);
		addEnemy(Aircraft::Avenger, 0.f, 6300.f);
		addEnemy(Aircraft::Avenger, 150.f, 6100.f);
		addEnemy(Aircraft::Avenger, -150.f, 6100.f);
		addEnemy(Aircraft::Raptor, 350.f, 6000.f);
		addEnemy(Aircraft::Raptor, -350.f, 6000.f);
		addEnemy(Aircraft::Avenger, 420.f, 6200.f);
		addEnemy(Aircraft::Avenger, -420.f, 6200.f);


		addEnemy(Aircraft::Avenger, 0.f, 5000.f);
		addEnemy(Aircraft::Avenger, 250.f, 5250.f);
		addEnemy(Aircraft::Avenger, -250.f, 5250.f);
		addEnemy(Aircraft::Raptor, 0.f, 5350.f);
		addEnemy(Aircraft::Raptor, 0.f, 5450.f);
		addEnemy(Aircraft::Raptor, 0.f, 5550.f);
		addEnemy(Aircraft::Avenger, -200.f, 5600.f);
		addEnemy(Aircraft::Avenger, 200.f, 5600.f);
		addEnemy(Aircraft::Avenger, 420.f, 5300.f);
		addEnemy(Aircraft::Avenger, -420.f, 5300.f);


		addEnemy(Aircraft::Avenger, 0.f, 4000.f);
		addEnemy(Aircraft::Raptor, 150.f, 4000.f);
		addEnemy(Aircraft::Raptor, -150.f, 4000.f);
		addEnemy(Aircraft::Avenger, 300.f, 4000.f);
		addEnemy(Aircraft::Avenger, -300.f, 4000.f);
		addEnemy(Aircraft::Avenger, 420.f, 4000.f);
		addEnemy(Aircraft::Avenger, -420.f, 4000.f);
		addEnemy(Aircraft::Avenger, 100.f, 3800.f);
		addEnemy(Aircraft::Avenger, -100.f, 3800.f);
		addEnemy(Aircraft::Avenger, 0.f, 3800.f);

		addEnemy(Aircraft::Avenger, 420.f, 3000.f);
		addEnemy(Aircraft::Avenger, -420.f, 3000.f);
		addEnemy(Aircraft::Raptor, -75.f, 3350.f);
		addEnemy(Aircraft::Raptor, 75.f, 3350.f);
		addEnemy(Aircraft::Raptor, 300.f, 3350.f);
		addEnemy(Aircraft::Raptor, -300.f, 3350.f);

		addEnemy(Aircraft::Avenger, -200.f, 2550.f);
		addEnemy(Aircraft::Avenger, 200.f, 2550.f);
		addEnemy(Aircraft::Avenger, 0.f, 2400.f);
		addEnemy(Aircraft::Avenger, 100.f, 2350.f);
		addEnemy(Aircraft::Avenger, -100.f, 2350.f);

		addEnemy(Aircraft::Raptor, 0.f, 2100.f);
		addEnemy(Aircraft::Raptor, +100.f, 2050.f);
		addEnemy(Aircraft::Raptor, -100.f, 2050.f);
		addEnemy(Aircraft::Avenger, 420.f, 2000.f);
		addEnemy(Aircraft::Avenger, -420.f, 2000.f);
		addEnemy(Aircraft::Avenger, 100.f, 1900.f);
		addEnemy(Aircraft::Avenger, -100.f, 1900.f);
		addEnemy(Aircraft::Avenger, -240.f, 1700.f);
		addEnemy(Aircraft::Avenger, 240.f, 1700.f);
		addEnemy(Aircraft::Raptor, 150.f, 1600.f);
		addEnemy(Aircraft::Raptor, -150.f, 1600.f);
		addEnemy(Aircraft::Avenger, 300.f, 1550.f);
		addEnemy(Aircraft::Avenger, -300.f, 1550.f);


		addEnemy(Aircraft::Raptor, 125.f, 1350.f);
		addEnemy(Aircraft::Raptor, 230.f, 1350.f);
		addEnemy(Aircraft::Raptor, -125.f, 1350.f);
		addEnemy(Aircraft::Raptor, -230.f, 1350.f);
		addEnemy(Aircraft::Raptor, 0.f, 1350.f);

	}

	else if (mLevel == 3)
	{
		addEnemy(Aircraft::Raptor, 0.f, 850.f);
		addEnemy(Aircraft::Raptor, +200.f, 1350.f);
		addEnemy(Aircraft::Raptor, -200.f, 1350.f);
		addEnemy(Aircraft::Avenger, 100.f, 1500.f);
		addEnemy(Aircraft::Avenger, -100.f, 1500.f);
		addEnemy(Aircraft::Avenger, -200.f, 1600.f);
		addEnemy(Aircraft::Avenger, 200.f, 1600.f);
		addEnemy(Aircraft::Raptor, 150.f, 1700.f);
		addEnemy(Aircraft::Avenger, 420.f, 1700.f);
		addEnemy(Aircraft::Avenger, -420.f, 1700.f);
		addEnemy(Aircraft::Raptor, -150.f, 1700.f);
		addEnemy(Aircraft::Avenger, 300.f, 1850.f);
		addEnemy(Aircraft::Avenger, -300.f, 1850.f);

		addEnemy(Aircraft::Raptor, 50.f, 2150.f);
		addEnemy(Aircraft::Raptor, -50.f, 2150.f);
		addEnemy(Aircraft::Raptor, 300.f, 2200.f);
		addEnemy(Aircraft::Raptor, -300.f, 2200.f);
		addEnemy(Aircraft::Raptor, 125.f, 2350.f);
		addEnemy(Aircraft::Raptor, -125.f, 2350.f);
		addEnemy(Aircraft::Avenger, 420.f, 2300.f);
		addEnemy(Aircraft::Avenger, -420.f, 2300.f);
		addEnemy(Aircraft::Avenger, 200.f, 2700.f);
		addEnemy(Aircraft::Avenger, -200.f, 2700.f);

		addEnemy(Aircraft::Raptor, 0.f, 3450.f);
		addEnemy(Aircraft::Raptor, 100.f, 3450.f);
		addEnemy(Aircraft::Raptor, -100.f, 3450.f);
		addEnemy(Aircraft::Raptor, 250.f, 3450.f);
		addEnemy(Aircraft::Raptor, -250.f, 3450.f);
		addEnemy(Aircraft::Avenger, 420.f, 3450.f);
		addEnemy(Aircraft::Avenger, -420.f, 3450.f);
		addEnemy(Aircraft::Avenger, 0.f, 3650.f);
		addEnemy(Aircraft::Avenger, 75.f, 3650.f);
		addEnemy(Aircraft::Avenger, -75.f, 3650.f);

		addEnemy(Aircraft::Avenger, 0.f, 4000.f);
		addEnemy(Aircraft::Avenger, 300.f, 4000.f);
		addEnemy(Aircraft::Avenger, -300.f, 4000.f);
		addEnemy(Aircraft::Raptor, 200.f, 4250.f);
		addEnemy(Aircraft::Raptor, -200.f, 4250.f);
		addEnemy(Aircraft::Avenger, 420.f, 4600.f);
		addEnemy(Aircraft::Avenger, -420.f, 4600.f);
		addEnemy(Aircraft::Avenger, 0.f, 4800.f);
		addEnemy(Aircraft::Avenger, 150.f, 4800.f);
		addEnemy(Aircraft::Avenger, -150.f, 4800.f);
		addEnemy(Aircraft::Raptor, 350.f, 4800.f);
		addEnemy(Aircraft::Raptor, -350.f, 4800.f);


		addEnemy(Aircraft::Raptor, 0.f, 9750.f);
		addEnemy(Aircraft::Raptor, +100.f, 9750.f);
		addEnemy(Aircraft::Raptor, -100.f, 9750.f);
		addEnemy(Aircraft::Avenger, 100.f, 9500.f);
		addEnemy(Aircraft::Avenger, -100.f, 9500.f);
		addEnemy(Aircraft::Avenger, -240.f, 9500.f);
		addEnemy(Aircraft::Avenger, 240.f, 9500.f);
		addEnemy(Aircraft::Avenger, 420.f, 9500.f);
		addEnemy(Aircraft::Avenger, -420.f, 9500.f);
		addEnemy(Aircraft::Raptor, 150.f, 9300.f);
		addEnemy(Aircraft::Raptor, -150.f, 9300.f);
		addEnemy(Aircraft::Avenger, 300.f, 9250.f);
		addEnemy(Aircraft::Avenger, -300.f, 9250.f);

		addEnemy(Aircraft::Raptor, 50.f, 8950.f);
		addEnemy(Aircraft::Raptor, -50.f, 8950.f);
		addEnemy(Aircraft::Raptor, 300.f, 8800.f);
		addEnemy(Aircraft::Raptor, -300.f, 8800.f);
		addEnemy(Aircraft::Raptor, 125.f, 8750.f);
		addEnemy(Aircraft::Raptor, -125.f, 8450.f);
		addEnemy(Aircraft::Avenger, 200.f, 8300.f);
		addEnemy(Aircraft::Avenger, -200.f, 8300.f);
		addEnemy(Aircraft::Avenger, 420.f, 8400.f);
		addEnemy(Aircraft::Avenger, -420.f, 8400.f);

		addEnemy(Aircraft::Raptor, 0.f, 7950.f);
		addEnemy(Aircraft::Raptor, 100.f, 7650.f);
		addEnemy(Aircraft::Raptor, -100.f, 7650.f);
		addEnemy(Aircraft::Raptor, 250.f, 7450.f);
		addEnemy(Aircraft::Raptor, -250.f, 7450.f);
		addEnemy(Aircraft::Avenger, 0.f, 7350.f);
		addEnemy(Aircraft::Avenger, 75.f, 7050.f);
		addEnemy(Aircraft::Avenger, -75.f, 7050.f);
		addEnemy(Aircraft::Avenger, 420.f, 7200.f);
		addEnemy(Aircraft::Avenger, -420.f, 7200.f);

		addEnemy(Aircraft::Avenger, 0.f, 6800.f);
		addEnemy(Aircraft::Avenger, 300.f, 6800.f);
		addEnemy(Aircraft::Avenger, -300.f, 6800.f);
		addEnemy(Aircraft::Raptor, 200.f, 6550.f);
		addEnemy(Aircraft::Raptor, -200.f, 6550.f);
		addEnemy(Aircraft::Avenger, 0.f, 6300.f);
		addEnemy(Aircraft::Avenger, 150.f, 6100.f);
		addEnemy(Aircraft::Avenger, 420.f, 6100.f);
		addEnemy(Aircraft::Avenger, -420.f, 6100.f);
		addEnemy(Aircraft::Avenger, -150.f, 6100.f);
		addEnemy(Aircraft::Raptor, 350.f, 6000.f);
		addEnemy(Aircraft::Raptor, -350.f, 6000.f);


		addEnemy(Aircraft::Avenger, 0.f, 5000.f);
		addEnemy(Aircraft::Avenger, 250.f, 5250.f);
		addEnemy(Aircraft::Avenger, -250.f, 5250.f);
		addEnemy(Aircraft::Raptor, 0.f, 5350.f);
		addEnemy(Aircraft::Raptor, 0.f, 5450.f);
		addEnemy(Aircraft::Avenger, 420.f, 5200.f);
		addEnemy(Aircraft::Avenger, -420.f, 5200.f);
		addEnemy(Aircraft::Raptor, 0.f, 5550.f);
		addEnemy(Aircraft::Avenger, -200.f, 5600.f);
		addEnemy(Aircraft::Avenger, 200.f, 5600.f);

	}

	else if (mLevel == 4)
	{
		//BOSS TIME

		addEnemy(Aircraft::C83, 0.f, 9750.f);
		addEnemy(Aircraft::C83, +100.f, 9750.f);
		addEnemy(Aircraft::C83, -100.f, 9750.f);
		addEnemy(Aircraft::C83, 100.f, 9500.f);
		addEnemy(Aircraft::C83, -100.f, 9500.f);
		addEnemy(Aircraft::C83, -240.f, 9500.f);
		addEnemy(Aircraft::C83, 240.f, 9500.f);
		addEnemy(Aircraft::C83, 150.f, 9300.f);
		addEnemy(Aircraft::C83, -150.f, 9300.f);
		addEnemy(Aircraft::C83, 420.f, 9150.f);
		addEnemy(Aircraft::C83, -420.f, 9150.f);
		addEnemy(Aircraft::C83, 300.f, 9250.f);
		addEnemy(Aircraft::C83, -300.f, 9250.f);
		addEnemy(Aircraft::C83, 375.f, 9250.f);
		addEnemy(Aircraft::C83, -375.f, 9250.f);

		addEnemy(Aircraft::C83, 50.f, 8950.f);
		addEnemy(Aircraft::C83, -50.f, 8950.f);
		addEnemy(Aircraft::C83, 300.f, 8800.f);
		addEnemy(Aircraft::C83, -300.f, 8800.f);
		addEnemy(Aircraft::C83, 125.f, 8750.f);
		addEnemy(Aircraft::C83, -125.f, 8450.f);
		addEnemy(Aircraft::C83, 200.f, 8300.f);
		addEnemy(Aircraft::C83, -200.f, 8300.f);
		addEnemy(Aircraft::C83, 375.f, 8300.f);
		addEnemy(Aircraft::C83, -375.f, 8300.f);
		addEnemy(Aircraft::C83, 420.f, 8550.f);
		addEnemy(Aircraft::C83, -420.f, 8550.f);


		addEnemy(Aircraft::C83, 0.f, 7950.f);
		addEnemy(Aircraft::C83, 100.f, 7650.f);
		addEnemy(Aircraft::C83, -100.f, 7650.f);
		addEnemy(Aircraft::C83, -175.f, 7450.f);
		addEnemy(Aircraft::C83, 205.f, 7650.f);
		addEnemy(Aircraft::C83, -175.f, 7450.f);
		addEnemy(Aircraft::C83, 0.f, 7375.f);
		addEnemy(Aircraft::C83, 75.f, 7050.f);
		addEnemy(Aircraft::C83, -75.f, 7050.f);
		addEnemy(Aircraft::C83, 100.f, 7300.f);
		addEnemy(Aircraft::C83, 420.f, 7250.f);
		addEnemy(Aircraft::C83, -420.f, 7250.f);
		addEnemy(Aircraft::C83, 355.f, 7050.f);
		addEnemy(Aircraft::C83, -375.f, 7050.f);

		addEnemy(Aircraft::C83, 0.f, 6800.f);
		addEnemy(Aircraft::C83, 300.f, 6800.f);
		addEnemy(Aircraft::C83, -300.f, 6800.f);
		addEnemy(Aircraft::C83, 200.f, 6550.f);
		addEnemy(Aircraft::C83, 420.f, 6550.f);
		addEnemy(Aircraft::C83, -420.f, 6550.f);
		addEnemy(Aircraft::C83, -200.f, 6550.f);
		addEnemy(Aircraft::C83, 0.f, 6300.f);
		addEnemy(Aircraft::C83, 150.f, 6100.f);
		addEnemy(Aircraft::C83, -150.f, 6100.f);
		addEnemy(Aircraft::C83, 375.f, 6000.f);
		addEnemy(Aircraft::C83, -375.f, 6000.f);


		addEnemy(Aircraft::C83, 0.f, 5000.f);
		addEnemy(Aircraft::C83, 250.f, 5250.f);
		addEnemy(Aircraft::C83, -250.f, 5250.f);
		addEnemy(Aircraft::C83, -50.f, 5300.f);
		addEnemy(Aircraft::C83, 50.f, 5300.f);
		addEnemy(Aircraft::C83, 0.f, 5375.f);
		addEnemy(Aircraft::C83, 420.f, 5550.f);
		addEnemy(Aircraft::C83, -420.f, 5550.f);
		addEnemy(Aircraft::C83, 180.f, 5550.f);
		addEnemy(Aircraft::C83, -180.f, 5550.f);
		addEnemy(Aircraft::C83, -200.f, 5700.f);
		addEnemy(Aircraft::C83, 200.f, 5700.f);
		addEnemy(Aircraft::C83, -375.f, 5700.f);
		addEnemy(Aircraft::C83, 375.f, 5700.f);




		addEnemy(Aircraft::C83, 0.f, 4000.f);
		addEnemy(Aircraft::C83, 150.f, 4000.f);
		addEnemy(Aircraft::C83, -150.f, 4000.f);
		addEnemy(Aircraft::C83, 300.f, 4000.f);
		addEnemy(Aircraft::C83, -300.f, 4000.f);
		addEnemy(Aircraft::C83, 375.f, 4600.f);
		addEnemy(Aircraft::C83, -375.f, 4600.f);
		addEnemy(Aircraft::C83, 420.f, 4350.f);
		addEnemy(Aircraft::C83, -420.f, 4350.f);



		addEnemy(Aircraft::C83, 100.f, 3800.f);
		addEnemy(Aircraft::C83, -100.f, 3800.f);
		addEnemy(Aircraft::C83, 0.f, 3800.f);
		addEnemy(Aircraft::C83, -75.f, 3350.f);
		addEnemy(Aircraft::C83, 75.f, 3350.f);
		addEnemy(Aircraft::C83, 150.f, 3350.f);
		addEnemy(Aircraft::C83, -150.f, 3350.f);
		addEnemy(Aircraft::C83, 300.f, 3350.f);
		addEnemy(Aircraft::C83, -300.f, 3350.f);
		addEnemy(Aircraft::C83, 375.f, 3350.f);
		addEnemy(Aircraft::C83, -375.f, 3350.f);
		addEnemy(Aircraft::C83, 420.f, 3550.f);
		addEnemy(Aircraft::C83, -420.f, 3550.f);

		addEnemy(Aircraft::C83, -200.f, 2550.f);
		addEnemy(Aircraft::C83, 200.f, 2550.f);
		addEnemy(Aircraft::C83, 0.f, 2400.f);
		addEnemy(Aircraft::C83, 100.f, 2350.f);
		addEnemy(Aircraft::C83, -100.f, 2350.f);
		addEnemy(Aircraft::C83, -375.f, 2350.f);
		addEnemy(Aircraft::C83, 375.f, 2350.f);

		addEnemy(Aircraft::C83, 0.f, 2100.f);
		addEnemy(Aircraft::C83, +100.f, 2050.f);
		addEnemy(Aircraft::C83, -100.f, 2050.f);
		addEnemy(Aircraft::C83, 100.f, 1900.f);
		addEnemy(Aircraft::C83, -100.f, 1900.f);
		addEnemy(Aircraft::C83, -240.f, 1700.f);
		addEnemy(Aircraft::C83, 240.f, 1700.f);
		addEnemy(Aircraft::C83, 150.f, 1600.f);
		addEnemy(Aircraft::C83, -150.f, 1600.f);
		addEnemy(Aircraft::C83, 300.f, 1550.f);
		addEnemy(Aircraft::C83, -300.f, 1550.f);
		addEnemy(Aircraft::C83, 375.f, 1550.f);
		addEnemy(Aircraft::C83, -375.f, 1550.f);


		addEnemy(Aircraft::C83, -320.f, 1250.f);
		addEnemy(Aircraft::C83, 320.f, 1250.f);
		addEnemy(Aircraft::C83, 125.f, 1350.f);
		addEnemy(Aircraft::C83, 230.f, 1350.f);
		addEnemy(Aircraft::C83, -125.f, 1350.f);
		addEnemy(Aircraft::C83, -230.f, 1350.f);
		addEnemy(Aircraft::C83, 0.f, 1350.f);
		addEnemy(Aircraft::C83, -425.f, 1350.f);
		addEnemy(Aircraft::C83, 425.f, 1350.f);
	}

	// Sort all enemies according to their y value, such that lower enemies are checked first for spawning
	std::sort(mEnemySpawnPoints.begin(), mEnemySpawnPoints.end(), [](SpawnPoint lhs, SpawnPoint rhs)
	{
		return lhs.y < rhs.y;
	});
}

void World::addEnemy(Aircraft::Type type, float relX, float relY)
{
	SpawnPoint spawn(type, mSpawnPosition.x + relX, mSpawnPosition.y - relY);
	mEnemySpawnPoints.push_back(spawn);
}

void World::spawnEnemies()
{
	// Spawn all enemies entering the view area (including distance) this frame
	while (!mEnemySpawnPoints.empty()
		&& mEnemySpawnPoints.back().y > getBattlefieldBounds().top)
	{
		SpawnPoint spawn = mEnemySpawnPoints.back();

		std::unique_ptr<Aircraft> enemy(new Aircraft(spawn.type, mTextures, mFonts));
		enemy->setPosition(spawn.x, spawn.y);
		enemy->setRotation(180.f);

		mSceneLayers[UpperAir]->attachChild(std::move(enemy));

		// Enemy is spawned, remove from the list to spawn
		mEnemySpawnPoints.pop_back();
	}
}

void World::destroyEntitiesOutsideView()
{
	Command command;
	command.category = Category::Projectile | Category::EnemyAircraft;
	command.action = derivedAction<Entity>([this](Entity& e, sf::Time)
	{
		if (!getBattlefieldBounds().intersects(e.getBoundingRect()))
			e.remove();
	});

	mCommandQueue.push(command);
}

void World::guideMissiles()
{
	// Setup command that stores all enemies in mActiveEnemies
	Command enemyCollector;
	enemyCollector.category = Category::EnemyAircraft;
	enemyCollector.action = derivedAction<Aircraft>([this](Aircraft& enemy, sf::Time)
	{
		if (!enemy.isDestroyed()) {
			mActiveEnemies.push_back(&enemy);
		}
	});

	// Setup command that guides all missiles to the enemy which is currently closest to the player
	Command missileGuider;
	missileGuider.category = Category::AlliedProjectile;
	missileGuider.action = derivedAction<Projectile>([this](Projectile& missile, sf::Time)
	{
		// Ignore unguided bullets
		if (!missile.isGuided())
			return;

		float minDistance = std::numeric_limits<float>::max();
		Aircraft* closestEnemy = nullptr;

		// Find closest enemy
		FOREACH(Aircraft* enemy, mActiveEnemies)
		{
			float enemyDistance = distance(missile, *enemy);

			if (enemyDistance < minDistance)
			{
				closestEnemy = enemy;
				minDistance = enemyDistance;
			}
		}

		if (closestEnemy)
			missile.guideTowards(closestEnemy->getWorldPosition());
	});

	// Push commands, reset active enemies
	mCommandQueue.push(enemyCollector);
	mCommandQueue.push(missileGuider);
	mActiveEnemies.clear();
}

sf::FloatRect World::getViewBounds() const
{
	return sf::FloatRect(mWorldView.getCenter() - mWorldView.getSize() / 2.f, mWorldView.getSize());
}

sf::FloatRect World::getBattlefieldBounds() const
{
	// Return view bounds + some area at top, where enemies spawn
	sf::FloatRect bounds = getViewBounds();
	bounds.top -= 100.f;
	bounds.height += 100.f;

	return bounds;
}

int World::getLevel()
{
	return mLevel;
}

sf::Int64 World::getScore()
{
	return mScore;
}

void  World::updateGame()
{
	mScore = 0;
	mLevel = 1;
	Aircraft::updateGame();
}

void World::increaseScore()
{
	if (mLevel == 2) mScore += 1000;
	else if (mLevel == 3) mScore += 2000;
	else if (mLevel == 4) mScore += 5000;
	else if (mLevel == 5) mScore +=10000;
}