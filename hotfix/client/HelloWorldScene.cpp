#include "HelloWorldScene.h"
#include "AppMacros.h"

USING_NS_CC;
USING_NS_CC_EXT;

Scene* HelloWorld::scene()
{
     return HelloWorld::create();
}

// on "init" you need to initialize your instance
bool HelloWorld::init()
{
    //////////////////////////////
    // 1. super init first
    if ( !Scene::init() )
    {
        return false;
    }
    
    auto visibleSize = Director::getInstance()->getVisibleSize();
    auto origin = Director::getInstance()->getVisibleOrigin();

    /////////////////////////////
    // 2. add a menu item with "X" image, which is clicked to quit the program
    //    you may modify it.

    // add a "close" icon to exit the progress. it's an autorelease object
    auto closeItem = MenuItemImage::create(
                                        "CloseNormal.png",
                                        "CloseSelected.png",
                                        CC_CALLBACK_1(HelloWorld::startDownload,this));
    
    closeItem->setPosition(origin + Vec2(visibleSize) - Vec2(closeItem->getContentSize() / 2));

    // create menu, it's an autorelease object
    auto menu = Menu::create(closeItem, nullptr);
    menu->setPosition(Vec2::ZERO);
    this->addChild(menu, 1);
    
    /////////////////////////////
    // 3. add your codes below...

    // add a label shows "Hello World"
    // create and initialize a label
    
    auto label = Label::createWithTTF("Hello World", "fonts/arial.ttf", TITLE_FONT_SIZE);
	proLabel = label;
    // position the label on the center of the screen
    label->setPosition(origin.x + visibleSize.width/2,
                            origin.y + visibleSize.height - label->getContentSize().height);

    // add the label as a child to this layer
    this->addChild(label, 1);

    // add "HelloWorld" splash screen"
    auto sprite = Sprite::create("HelloWorld.png");

    // position the sprite on the center of the screen
    sprite->setPosition(Vec2(visibleSize / 2) + origin);

    // add the sprite as a child to this layer
    this->addChild(sprite);
    
	_am = AssetsManagerEx::create("E:/Cocos/manifest/project.manifest", "E:/Cocos/download");
	_am->retain();
    return true;
}

void HelloWorld::menuCloseCallback(Ref* sender)
{
    //Director::getInstance()->end();/*
#if (CC_TARGET_PLATFORM == CC_PLATFORM_IOS)
    exit(0);
#endif
}

void HelloWorld::startDownload(Ref* sender)
{
	if (!_am->getLocalManifest()->isLoaded())
	{
		CCLOG("Fail to update assets, step skipped.");
		onLoadEnd();
	}
	else
	{
		int testIndex = 1;
		_amListener = cocos2d::extension::EventListenerAssetsManagerEx::create(_am, [testIndex, this](EventAssetsManagerEx* event) {
			static int failCount = 0;
			switch (event->getEventCode())
			{
			case EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST:
			{
				CCLOG("No local manifest file found, skip assets update.");
				this->onLoadEnd();
			}
			break;
			case EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION:
			{
				std::string assetId = event->getAssetId();
				float percent = event->getPercent();
				std::string str;
				if (assetId == AssetsManagerEx::VERSION_ID)
				{
					str = StringUtils::format("Version file: %.2f", percent) + "%";
				}
				else if (assetId == AssetsManagerEx::MANIFEST_ID)
				{
					str = StringUtils::format("Manifest file: %.2f", percent) + "%";
				}
				else
				{
					str = StringUtils::format("%.2f", percent) + "%";
					CCLOG("%.2f Percent", percent);
				}
				
				if (this->proLabel != nullptr)
					this->proLabel->setString(str);
			}
			break;
			case EventAssetsManagerEx::EventCode::ERROR_DOWNLOAD_MANIFEST:
			case EventAssetsManagerEx::EventCode::ERROR_PARSE_MANIFEST:
			{
				CCLOG("Fail to download manifest file, update skipped.");
				this->onLoadEnd();
			}
			break;
			case EventAssetsManagerEx::EventCode::ALREADY_UP_TO_DATE:
			case EventAssetsManagerEx::EventCode::UPDATE_FINISHED:
			{
				CCLOG("Update finished. %s", event->getMessage().c_str());
				this->onLoadEnd();
			}
			break;
			case EventAssetsManagerEx::EventCode::UPDATE_FAILED:
			{
				CCLOG("Update failed. %s", event->getMessage().c_str());

				failCount++;
				if (failCount < 5)
				{
					_am->downloadFailedAssets();
				}
				else
				{
					CCLOG("Reach maximum fail count, exit update process");
					failCount = 0;
					this->onLoadEnd();
				}
			}
			break;
			case EventAssetsManagerEx::EventCode::ERROR_UPDATING:
			{
				CCLOG("Asset %s : %s", event->getAssetId().c_str(), event->getMessage().c_str());
			}
			break;
			case EventAssetsManagerEx::EventCode::ERROR_DECOMPRESS:
			{
				CCLOG("%s", event->getMessage().c_str());
			}
			break;
			default:
				break;
			}
		});
		Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(_amListener, 1);

		_am->update();
	}

}
void HelloWorld::onLoadEnd()
{
	this->proLabel->setString("download finish");
}

//test
