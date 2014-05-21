Choose-Your-Path-Apache-Module
==============================

Apache Module for creating a Choose Your Path game using only the site conf file.

To install you need to compile and install into apache. This is easiest using the apxs compile script. 
Run this command with apxs:

<code>
sudo apxs -ica -n choose_your_path mod_choose_your_path.c && sudo service apache2 restart
</code>

This will compile the module, install it into apache and then restart apache.

The modules looks for these variables to be defined on each path defined in your site configuration. 

* SetHandler choose-handler // Only needs to be set on root path
* LevelTitle "" // Title of the level
* LevelDescription "" // The text to show in your level
* MoveRight "{path}" "{action title}" // choose to move left
* MoveLeft "{path}" "{action title}" // optional
* Treasure "{num}" // ammount to reward player
* Damage "{num}" // amount to take off life

Sample configuration for your site .conf file.

```
        <Location "/cyp">
            SetHandler choose-handler
            LevelTitle "Stage 1: Welcome Home"
            LevelDescription "Stage 1"
            MoveRight "/cyp/stage2" "Stage 2"
            Treasure "10"
            Damage "20"
        </Location>

        <Location "/cyp/stage2">
            LevelTitle "Stage 2: Steps to a house."
            LevelDescription "Stage 2"
            MoveLeft "/cyp" "Back to stage 1."
            MoveRight "/cyp/stage3" "Stage 3."
            Treasure "20"
            Damage "40"
        </Location>

        <Location "/cyp/stage3">
            LevelTitle "Stage 3: The Empty Room"
            LevelDescription "The room is dark, the only light you see is from a little candle flickering from wind of an open window. In the back corder, you see the outline of a doorframe."
            MoveLeft "/cyp/stage2" "Back to stage 2."
            MoveRight "/cyp/stage4" "Stage 4."
            Treasure "40"
            Damage "80"
        </Location>
```
