Choose-Your-Path-Apache-Module
==============================

Apache Module for creating a Choose Your Path game using only the site conf file.

To install you need to compile and install into apache. This is easiest using the apxs compile script. 
Run this command with apxs:
sudo apxs -ica -n choose_your_path mod_choose_your_path.c && sudo service apache2 restart

This will compile the module, install it into apache and then restart apache.
