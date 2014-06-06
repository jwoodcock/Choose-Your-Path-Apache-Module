/*
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * mod_choose_your_path.c
 * Choose Your Path
 * copyright Jacques Woodcock <jacques@kitewebconsulting.com>
 * This module allows you to create a Choose Your Path game using configuration settings in your vHost file using the 
 * <Location> or <Directory> settings. 
 * A sample configuration:
 * <Location "/cyp/stage2">
 *     LevelTitle "Stage 2: Steps to a house."
 *     LevelDescription "Stage 2"
 *     MoveLeft "/cyp" "Back to stage 1."
 *     MoveRight "/cyp/stage3" "Stage 3."
 *     Treasure "0"
 *     Damage "20"
 * </Location>
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include "apr_hash.h"
#include "ap_config.h"
#include "ap_provider.h"
#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"

/*
 ~~~~~~~~~~~~~~~~~~~~~~~
 Configuration
 ~~~~~~~~~~~~~~~~~~~~~~~
 */
typedef struct
{
    const char	*level_title; // Level story title 
    const char	*level_description; // Level story description
    const char	*move_left; // path to move left
    const char	*move_left_title; // path to move left
    const char	*move_right; // path to move right 
    const char	*move_right_title; // path to move right 
    const char	*treasure; // treasure to add
    const char	*damage; // damage recieved
    char        *theme_template; // raw template
} choose_config;

/*
 ~~~~~~~~~~~~~~~~~~~~~~~
 Prototypes
 ~~~~~~~~~~~~~~~~~~~~~~~
 */
static void   	register_hooks(apr_pool_t *pool);
static int    	choose_handler(request_rec *r);
int 		    print_table(void* rec, const char* key, const char* value);
static 		    choose_config config;

// Set config values
const char 	*choose_set_damage(cmd_parms *cmd, void *cfg, const char *arg);
const char 	*choose_set_treasure(cmd_parms *cmd, void *cfg, const char *arg);
const char 	*choose_set_move_left(cmd_parms *cmd, void *cfg, const char *arg1, const char *arg2);
const char 	*choose_set_move_right(cmd_parms *cmd, void *cfg, const char *arg1, const char *arg2);
const char 	*choose_set_level_title(cmd_parms *cmd, void *cfg, const char *arg);
const char 	*choose_set_level_description(cmd_parms *cmd, void *cfg, const char *arg);
const char 	*choose_set_template(cmd_parms *cmd, void *cfg, const char *arg);

void* 		choose_default_conf(apr_pool_t* pool, char* context);
void* 		choose_merge_conf(apr_pool_t* pool, void* BASE, void* ADD);

// Custom Helper Methods
char *replace_str(const char *str, const char *old, const char *new);
/*
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Handler for intaking directives
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
static const command_rec choose_directives[] =
{
    AP_INIT_TAKE2("moveLeft", choose_set_move_left, NULL, ACCESS_CONF, "Define where moving left will take the player."),
    AP_INIT_TAKE2("moveRight", choose_set_move_right, NULL, ACCESS_CONF, "Define where moving right will take the player."),
    AP_INIT_TAKE1("treasure", choose_set_treasure, NULL, ACCESS_CONF, "Reward the player with treasure for making it to this level."),
    AP_INIT_TAKE1("levelDescription", choose_set_level_description, NULL, ACCESS_CONF, "Set the description of the level and what to do."),
    AP_INIT_TAKE1("levelTitle", choose_set_level_title, NULL, ACCESS_CONF, "Set the title of the level and what to do."),
    AP_INIT_TAKE1("damage", choose_set_damage, NULL, ACCESS_CONF, "Inflict damage on the player."),
    AP_INIT_TAKE1("template", choose_set_template, NULL, ACCESS_CONF, "Define the theme for the game."),
    { NULL }
};

/*
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Module Name Tag
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
module AP_MODULE_DECLARE_DATA choose_your_path_module =
{
    STANDARD20_MODULE_STUFF,
    choose_default_conf,        /* Per-server configuration handler */
    choose_merge_conf,          /* Merge handler for per-server configurations */
    NULL,			            /* Any directives we may have for httpd */
    NULL,
    choose_directives,
    register_hooks     		    /* Our hook registering function */
};

/*
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Register hooks
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
static void register_hooks(apr_pool_t *pool)
{
    
    /*ap_hook_child_init: Place a hook that executes when a child process is spawned (commonly used for initializing modules after the server has forked)
    ap_hook_pre_config: Place a hook that executes before any configuration data has been read (very early hook)
    ap_hook_post_config: Place a hook that executes after configuration has been parsed, but before the server has forked
    ap_hook_translate_name: Place a hook that executes when a URI needs to be translated into a filename on the server (think mod_rewrite)
    ap_hook_quick_handler: Similar to ap_hook_handler, except it is run before any other request hooks (translation, auth, fixups etc)
    ap_hook_log_transaction: Place a hook that executes when the server is about to add a log entry of the current request*/

    /* Define the default configuration */
    ap_hook_handler(choose_handler, NULL, NULL, APR_HOOK_LAST);
}

/*
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Handler for Web Request
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
static int choose_handler(request_rec *r)
{

    // Make sure the request sent is for this handler, if not decline
    if (!r->handler || strcmp(r->handler, "choose-handler")) return (DECLINED);

    /* ~~~~~~~~
     Initiate Properties
     ~~~~~~~~*/
    int             i = 0; // loop for knowing which cookie value we are in
    int             show = 1; // flag to show content or not if player did not start at beginning 
    int				config_damage; // int to hold the value of damange to perform math on
    int				cookie_health; // int to hold the value of health to perform math on
    int 			cookie_treasure; // int to hold the cookie treasure amount so you know maths
    int				config_treasure; // int to hold conig treasure amount so maths again
    char 			buffer[10]; // buffer to convert after maths back to str to add to cookie str
    char			cookie[80]; // to hold the full cookie string to output with header
    char			*cookie_values; // char to hold split'd string to loop through
    char 			*cookie_data_in; // cookie data in a usable form to perform a split on
    char			*str_to_int_pointer; // buffer needed to convert string to int
    const char		*cookie_data = apr_table_get(r->headers_in, "Cookie"); // the actual cookie data

    // get context aware config
    choose_config *config = (choose_config *) ap_get_module_config(
        r->per_dir_config,
        &choose_your_path_module
    );

    // check for a cookie, gets data and removes it
    if (cookie_data && cookie_data != NULL) {
        // remove any cookie that was set prior
        //apr_table_unset(r->headers_out, "Set-Cookie");
        // copy the cookie data to format that works with strtok
        cookie_data_in = strdup(cookie_data);
        // split string into walkable table 
        cookie_values = strtok(cookie_data_in, "&");
        while(cookie_values) {
            // 0 is the location for the treasure data
            if (i == 0 && cookie_values != 0) {
                cookie_treasure = strtol(cookie_values, &str_to_int_pointer, 0); // get cookie treasure into an int
                config_treasure = strtol(config->treasure, &str_to_int_pointer, 0); // get config treasure into an int
                config_treasure += cookie_treasure; // now do the maths
                sprintf(buffer, "%i", config_treasure); // convert final treasure count to char
                strcpy(cookie, buffer); // then add it to the cookie which we will save after this request
            }
            // 1 is the location for the damage
            if (i == 1 && cookie_values != 0) {
                cookie_health = strtol(cookie_values, &str_to_int_pointer, 0); // get health into an int
                config_damage = strtol(config->damage, &str_to_int_pointer, 0); // get damage taken to int
                config_damage = cookie_health - config_damage; // do maths again
                sprintf(buffer, "%i", config_damage); // convert back to string
                strcat(cookie, "&"); // add splitter to cookie string
                strcat(cookie, buffer);  // add updated health to cookie
            }
            // move the step up for cookie_values
            cookie_values = strtok(NULL, "&");
            ++i;
        }
    } else {
        // No cookie means this might be the start of the game
        if (r->filename != "/var/www/html/cyp") {
            show = 0;
        }
        strcpy(cookie, "0&1000");
    }

    // assign the get variables
    apr_table_t *GET;

    // Set updated cookie
    apr_table_set(r->headers_out, "Set-Cookie", cookie);

    // set the response content type
    ap_set_content_type(r, "text/html");

    // If we have cookie and can show the page but no template was provided
    // show the default layout
    if (show == 1 && NULL == config->theme_template) {
        // Game title
        ap_rprintf(r, "<pre> @@@@@@@ @@@  @@@  @@@@@@   @@@@@@   @@@@@@ @@@@@@@@    @@@ @@@  @@@@@@  @@@  @@@ @@@@@@@     @@@@@@@   @@@@@@  @@@@@@@ @@@  @@@<br />");
        ap_rprintf(r, "!@@      @@!  @@@ @@!  @@@ @@!  @@@ !@@     @@!         @@! !@@ @@!  @@@ @@!  @@@ @@!  @@@    @@!  @@@ @@!  @@@   @!!   @@!  @@@<br />");
        ap_rprintf(r, "!@!      @!@!@!@! @!@  !@! @!@  !@!  !@@!!  @!!!:!       !@!@!  @!@  !@! @!@  !@! @!@!!@!     @!@@!@!  @!@!@!@!   @!!   @!@!@!@!<br />");
        ap_rprintf(r, ":!!      !!:  !!! !!:  !!! !!:  !!!     !:! !!:           !!:   !!:  !!! !!:  !!! !!: :!!     !!:      !!:  !!!   !!:   !!:  !!!<br />");
        ap_rprintf(r, " :: :: :  :   : :  : :. :   : :. :  ::.: :  : :: ::       .:     : :. :   :.:: :   :   : :     :        :   : :    :     :   : : </pre>");
        // Treasure stat
        ap_rprintf(r, "Treasure: %i Int: %i<br />", i, config_treasure); // temp output to show total treasure
        // Health stat
        ap_rprintf(r, "Health: %i Int: %i<br />", i, config_damage); // output to see results
        // Start building the page
        ap_rprintf(r, "<h3>%s</h3>", config->level_title);
        ap_rprintf(r, "<p>%s</p>", config->level_description);
        ap_rprintf(r,
            "<p><--<a href=\"%s\">%s</a> (O) <a href=\"%s\">%s</a> --></p>",
            config->move_left,
            config->move_left_title,
            config->move_right,
            config->move_right_title
        );
        ap_rprintf(r, "<p>--Stats--</p>");
        ap_rprintf(r, "<p>Gained %s treasure</p>", config->treasure);
        ap_rprintf(r, "<p>Took %s damange</p>", config->damage);
    // If we have a cookie and can show the page and also have a provided
    // template, then process template and show the page
    } else if (show == 1 && NULL != config->theme_template) {
        long length;
        char *old_template = 0;
        length = strlen(config->theme_template);
        old_template= malloc(length);
        strcpy(old_template, config->theme_template);
        // Replace place holders
        if (old_template) {
            // title
            char* processed_template = replace_str(
                old_template,
                "{{title}}",
                "Choose Your Path"
            );
            // health
            processed_template = replace_str(
                processed_template,
                "{{health}}",
                config->damage
            );
            // treasure
            processed_template = replace_str(
                processed_template,
                "{{treasure}}",
                config->treasure
            );
            // choices
            char choices[100];
            snprintf(choices, sizeof(choices), "<p><--<a href=\"%s\">%s</a> (O) <a href=\"%s\">%s</a> --></p>",
                config->move_left,
                config->move_left_title,
                config->move_right,
                config->move_right_title
            );
            processed_template = replace_str(
                processed_template,
                "{{choices}}",
                choices
            );
            // stage title
            processed_template = replace_str(
                processed_template,
                "{{stageTitle}}",
                config->level_title
            );
            // stage description
            processed_template = replace_str(
                processed_template,
                "{{description}}",
                config->level_description
            );
            //char *replace_str(const char *str, const char *old, const char *new)
            ap_rprintf(r, "%s", processed_template);
            free(processed_template);
        }
    // No cookie means we need to start over
    } else if (show == 0) {
        ap_rprintf(r, "<h2>You must start at the beginning.<br /><a href='/cyp'>Start Here</a></h2><br /><br /><br >");
    }

    return OK;
}

/*
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 Config value setters and config merge
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/* Set the move treasure */
const char *choose_set_damage(cmd_parms *cmd, void *cfg, const char *arg)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->damage = arg;
    }
    return NULL;
}

/* Set the move treasure */
const char *choose_set_treasure(cmd_parms *cmd, void *cfg, const char *arg)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->treasure = arg;
    }
    return NULL;
}

/* Set the move right */
const char *choose_set_move_right(cmd_parms *cmd, void *cfg, const char *arg1, const char *arg2)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->move_right = arg1;
        conf->move_right_title = arg2;
    }
    return NULL;
}

/* Set the move left */
const char *choose_set_move_left(cmd_parms *cmd, void *cfg, const char *arg1, const char *arg2)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->move_left = arg1;
        conf->move_left_title = arg2;
    }
    return NULL;
}

/* Set the level description */
const char *choose_set_level_description(cmd_parms *cmd, void *cfg, const char *arg)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->level_description = arg;
    }
    return NULL;
}

/* Set the level title */
const char *choose_set_level_title(cmd_parms *cmd, void *cfg, const char *arg)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        conf->level_title = arg;
    }
    return NULL;
}

/* Set the template */
const char *choose_set_template(cmd_parms *cmd, void *cfg, const char *arg)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    choose_config    *conf = (choose_config *) cfg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    if (conf) {
        FILE * template_file = fopen (arg, "rb");
        long length;
        conf->theme_template = 0;
        if (template_file) {
            fseek(template_file, 0, SEEK_END);
            length = ftell(template_file);
            fseek(template_file, 0, SEEK_SET);
            conf->theme_template = malloc(length);
            if (conf->theme_template) {
                if (!fread(conf->theme_template, 1, length, template_file)) {
                    // something
                }
            }
            fclose (template_file);
        }
    }
    return NULL;
}

/* Create Conf */
void *choose_default_conf(apr_pool_t *pool, char *context) {

    context = context ? context : "(undefined context)";
    choose_config *cfg = apr_pcalloc(pool, sizeof(choose_config));

    return cfg;
}

/* Merge configs */
void *choose_merge_conf(apr_pool_t *pool, void *BASE, void *ADD) {

    choose_config* base = (choose_config *) BASE ; /* This is what was set in the parent context */
    choose_config* add = (choose_config *) ADD ;   /* This is what is set in the new context */
    choose_config* conf = (choose_config *) choose_default_conf(pool, "Merged configuration"); /* This will be the merged configuration */

    /* Merge configurations */
    conf->level_description = ( add->level_description == NULL ) ? base->level_description : add->level_description;
    conf->level_title = ( add->level_title == NULL ) ? base->level_title : add->level_title;
    conf->move_left = ( add->move_left == NULL ) ? base->move_left : add->move_left;
    conf->move_left_title = ( add->move_left_title == NULL ) ? base->move_left_title : add->move_left_title;
    conf->move_right = ( add->move_right == NULL ) ? base->move_right : add->move_right;
    conf->move_right_title = ( add->move_right_title == NULL ) ? base->move_right_title : add->move_right_title;
    conf->treasure = ( add->treasure == NULL ) ? base->treasure : add->treasure;
    conf->damage = ( add->damage == NULL ) ? base->damage : add->damage;
    conf->theme_template = ( add->theme_template == NULL ) ? base->theme_template : add->theme_template;

    // Look for nulls on configs that are not required
    if (conf->move_left == NULL) {
        conf->move_left = "";
        conf->move_left_title = "";
    }
    
    return conf;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   Custom Methods
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   */
char *replace_str(const char *str, const char *old, const char *new)
{
    char *ret, *r;
    const char *p, *q;
    size_t oldlen = strlen(old);
    size_t count, retlen, newlen = strlen(new);

    if (oldlen != newlen) {
        for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
            count++;
        /* this is undefined if p - str > PTRDIFF_MAX */
        retlen = p - str + strlen(p) + count * (newlen - oldlen);
    } else
        retlen = strlen(str);

    if ((ret = malloc(retlen + 1)) == NULL)
        return NULL;

    for (r = ret, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen) {
        /* this is undefined if q - p > PTRDIFF_MAX */
        ptrdiff_t l = q - p;
        memcpy(r, p, l);
        r += l;
        memcpy(r, new, newlen);
        r += newlen;
    }
    strcpy(r, p);

    return ret;
}
