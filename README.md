[![Build Status](https://travis-ci.org/raine0524/base_platform.svg?branch=master)](https://travis-ci.org/raine0524/base_platform)

这是一个遵循SOA架构思想并使用现代c++标准库实现的轻量级开发框架，这个框架不仅包含了网络通信模块及后台服务开发过程中经常会用到的一些组件，并且定义了一套服务之间进行交互的模型，此外还提供了一些分布式系统中经常会使用的服务(目前只实现了名字发现)。

这个开发框架的目标是将控制逻辑与业务逻辑进行解耦，从而在框架提供的基本组件上能够快速的迭代出业务系统的原型，由于框架本身基于c++1x标准库以及linux系统api实现，没有过重的历史包袱，也不考虑跨平台，所以很容易阅读修改以及维护。

框架本身的设计原则就是只提供最一般的功能，从而能够最大程度的适配各种应用场景，但这也意味着组件提供的接口并不丰富，从而需要在业务开发中针对特定场景定制更强的控制逻辑(通常的做法是在框架和服务中间再封装一层，这是一个针对业务场景定制的库)，而这也符合SOA的原则，并且也不会让这个框架显得臃肿而难以维护