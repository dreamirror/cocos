#ifndef __HELLOWORLD_SCENE_H__
#define __HELLOWORLD_SCENE_H__

#include "cocos2d.h"
#include "cocos-ext.h"

class HelloWorld : public cocos2d::Scene
{
public:
    virtual bool init() override;

    static cocos2d::Scene* scene();
	cocos2d::Label* proLabel;
    // a selector callback
    void menuCloseCallback(Ref* sender);
	void startDownload(Ref* sender);
	//void startDownloadCallback(Ref* sender);
	void onLoadEnd();
    // implement the "static create()" method manually

	cocos2d::extension::AssetsManagerEx* _am;
	cocos2d::extension::EventListenerAssetsManagerEx* _amListener;

    CREATE_FUNC(HelloWorld);
};

#endif // __HELLOWORLD_SCENE_H__
//test