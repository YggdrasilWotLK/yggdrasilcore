# <img alt="image" src="https://github.com/user-attachments/assets/6f96adf9-1867-4ca6-b627-1bd0ab7ee494" />

## Introduction

Yggdrasilcore is an AzerothCore-based open-source game server application and framework designed for emulating support for connections and packets from and sending emulated information to the original abandonware WotLK 3.3.5a client. Read more about the upstream project [here](https://github.com/azerothcore/azerothcore-wotlk).

Our latest mass merge from AzerothCore was [up until this commit in the Shadows branch](https://github.com/mod-shadows/azerothcore-wotlk/commit/39187083b04ede6a095e32b39ac4ae4e22dee22f) on 2025-10-25. Only cherry picks are planned hereafter.

Read more about Yggdrasil WoW and see Yggdrasilcore in action [here](https://yggdrasilwow.com/).


## Build Status

<img width="171" height="35" alt="image" src="https://github.com/user-attachments/assets/e1078d72-c9e2-42d3-a6ce-5bd70cd8edb4" />
<br><br>
We build every commit on a local Dockerized Ubuntu 24.04 machine before anything is merged to master. 

## Philosophy

The AzerothCore project generally uses a rapid, rolling release system, which sees minimal testing between commits (apart from CI checks). Resultingly, many AzerothCore-based servers that adhere purely to the upstream codebase receive breaking changes more or less weekly. At Yggdrasilcore, we have inverted this logic to instead change purely that which needs changing - and we have decided that stable, functional code, such as threadsafe monoliths, doesn't need to be broken up/refactored/changed past that which is necessary for Blizzlike functionality. (Curiously enough, this train of thought is similar to the original SunwellCore split from TrinityCore, which in turn later birthed AzerothCore, and alas, here we are.)

Note that Blizzlike in this regard isn't interpreted as purely based on sniffs. We use more data sources and have a more lax policy on what to accept in our codebase, and consider using Wowhead information or YouTube videos for approximate details as satisfactory. 

### Modules

As an AzerothCore-based framework, Yggdrasilcore is designed to be highly modular, allowing developers to extend and customize the game to suit their preferences or create unique gameplay experiences. This flexibility enables the addition of custom features, content, and modifications. Most AzerothCore modules are supported to varying degrees. See their catalogue here [Module Catalogue](https://www.azerothcore.org/catalogue.html#/), or browse the modules we have adapted to Yggdrasilcore in our list of repos [here](https://github.com/YggdrasilWotLK?tab=repositories).

## Installation

Yggdarsilcore's installation instructions are the same as upstream. Detailed installation instructions are available [here](http://www.azerothcore.org/wiki/installation).

## Contributing

Yggdrasilcore is a privately managed repository. We do not recommend emulation enthusiasts to follow it. Instead, we recommend that enthusiasts contribute to the upstream AzerothCore project. See their information on how to contribute [here](https://www.azerothcore.org/wiki/contribute).

## Authors & Contributors

Authors from 2016 are available in our Git history. For information prior to 2016, see upstream project information here: [authors](https://github.com/azerothcore/azerothcore-wotlk/blob/master/AUTHORS) file for more details.

## License

- Source: Contrary to some of the source documentation and previous information distributed by AzerothCore, all Yggdrasilcore source components are licensed under GNU GPL v2. 
- Intellectual property: Yggdrasilcore is not related to Blizzard Entertainment. Yggdrasil WoW and its derivative projects lay no claim to Blizzard Entertainment's copyrights and intellectual property, and operate this project solely as an avenue for exploring the functionality of the abandonware WotLK 3.3.5a client in an educational capacity.
