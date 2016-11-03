<?php

/**
 * @file
 * Provides API functions for the Google Image Sitemap module.
 *
 * This file contains all the function for this module which aren't Drupal hook
 * implementations.
 */

/**
 * Constant definitions.
 */

// Define the path at which image sitemaps are published.
define('BETTER_GOOGLE_IMAGE_SITEMAP_SITEMAP_PATH', 'image-sitemap.xml');

// Define the administration base path for the module.
define('BETTER_GOOGLE_IMAGE_SITEMAP_ADMIN_PATH', 'admin/config/search/image-sitemap');

// Define the batch size for multiple record updates.
define('BETTER_GOOGLE_IMAGE_SITEMAP_BATCH_UPDATE_SIZE', 100);

// Define the default index/don't index behaviour for new node types.
define('BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_NEW_NODES_BY_DEFAULT', TRUE);

// Define the default submit/don't submit to search engines behaviour.
define('BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_ON_CRON_BY_DEFAULT', TRUE);

// Define the button IDs for the different index steps.
define('BETTER_GOOGLE_IMAGE_SITEMAP_FIND_NEW_NODES_TO_INDEX', 'findnewnodestoindex');
define('BETTER_GOOGLE_IMAGE_SITEMAP_REMOVE_DELETED_NODES', 'removedeletednodes');
define('BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_IMAGES', 'indeximages');
define('BETTER_GOOGLE_IMAGE_SITEMAP_GENERATE_SITEMAPS', 'generatesitemaps');
define('BETTER_GOOGLE_IMAGE_SITEMAP_FLAG_FOR_REBUILD', 'flagforrebuild');
define('BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_SITEMAPS', 'submitsitemaps');

// Define constants for rebuild and update needed/not needed because Drupal's
// db_schema API doesn't, weirdly, have a boolean datatype.
define('BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NEEDED', 1);
define('BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NOT_NEEDED', 0);
define('BETTER_GOOGLE_IMAGE_SITEMAP_REINDEX_NEEDED', 1);
define('BETTER_GOOGLE_IMAGE_SITEMAP_REINDEX_NOT_NEEDED', 0);

// The XML header which prefixes all sitemaps.
define('BETTER_GOOGLE_IMAGE_SITEMAP_XML_HEADER', '<?xml version="1.0" encoding="UTF-8"?>');

/**
 * Creates empty sitemap entries for languages which don't yet have a sitemap.
 *
 * @return int
 *   A count of the number of sitemap entries created.
 */
function better_google_image_sitemap_create_empty_sitemaps() {
  $counter = 0;

  // First, check that all languages have sitemap entries.
  $languages = language_list();

  // Get an array of the sitemaps already built so we can create empty sitemap
  // records.
  $existing_sitemap_records = db_select('better_google_image_sitemap_sitemaps', 'bcs')
    ->fields('bcs', array('language'))
    ->execute()
    ->fetchAllAssoc('language');

  foreach ($languages as $language_id => $language) {
    // If the language doesn't already have a sitemap record, add it now.
    if (!array_key_exists($language_id, $existing_sitemap_records)) {
      db_insert('better_google_image_sitemap_sitemaps')
        ->fields(array(
          'language' => $language_id,
          'rebuild_needed' => BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NEEDED,
          'updated' => REQUEST_TIME,
          'xml' => '',
        ))
        ->execute();

      $counter++;
    }
  }

  return $counter;
}

/**
 * Flag all sitemaps as needing to be rebuilt, and remove all indexed images.
 */
function better_google_image_sitemap_flag_for_rebuild() {
  // Remove all indexed images.
  db_delete('better_google_image_sitemap_images')
    ->execute();

  db_update('better_google_image_sitemap_sitemaps')
    ->fields(array(
      'rebuild_needed' => BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NEEDED,
    ))
    ->execute();
}

/**
 * Generate sitemaps for all languages which are flagged as requiring update.
 *
 * @param null $time_to_finish
 *   A timestamp of when we must finish processing by, if this task must not
 *   take longer than a certain amount of time, or NULL if it is allowed to
 *   keep going until it's done.
 *
 * @return int
 *   The number of records processed.
 */
function better_google_image_sitemap_generate_sitemaps($time_to_finish = NULL) {
  // Lastly, generate sitemaps for all languages which are flagged as requiring
  // update, save the sitemaps to the better_google_image_sitemap_sitemaps
  // table, set their updated timestamp to now, and set their "needs update"
  // flag to false. If maximum execution time is > 0, we must stop
  // processing after 100% of the time has elapsed since starting the timer.

  // Populate empty sitemaps.
  better_google_image_sitemap_create_empty_sitemaps();

  // Create a counter so we can tell how many records we've processed.
  $counter = 0;

  // Get a list of any sitemaps which require rebuild.
  $sitemaps_to_rebuild = better_google_image_sitemap_sitemaps_for_rebuild();

  // Get all languages.
  $languages = language_list();

  // Loop through each language and rebuild its site map for each language.
  foreach ($sitemaps_to_rebuild as $sitemap_to_rebuild) {
    // Select * from {better_google_image_sitemap_images} bci where language =
    // {language} group by nid order by nid asc.
    $query = db_select('better_google_image_sitemap_images', 'bci')
      ->fields('bci')
      ->condition('bci.language', $sitemap_to_rebuild);

    $results = $query->execute()->fetchAll();

    // Create an array whose keys are the node IDs for the files, and each node
    // ID's array contains an array of file IDs, which in turn contain the file
    // records from the {better_google_image_sitemap_images} table.
    $files_by_nid = array();

    foreach ($results as $result) {
      if (!array_key_exists($result->nid, $files_by_nid)) {
        $files_by_nid[$result->nid] = array();
      }

      // For each result, add its db result object into the array key for the
      // file's node ID.
      $files_by_nid[$result->nid][$result->fid] = $result;
    }

    $nodes = array();

    // Loop through the array of node IDs -> files.
    foreach ($files_by_nid as $nid => $files_by_fid) {
      $node_data = array();

      // For each node, create the node's XML header and add it to the output.
      $node_data['loc'] = array(
        '#value' => url('node/' . $nid, array(
          'absolute' => TRUE,
          'language' => $languages[$sitemap_to_rebuild],
        )),
      );

      // Loop through each file in the node, creating its XML data and adding it
      // to the output.
      $node_images = array();

      foreach ($files_by_fid as $file_by_fid) {
        $node_images[] = array(
          'image:loc' => array(
            '#value' => $file_by_fid->image_url,
          ),
          'image:title' => array(
            '#value' => $file_by_fid->image_title,
          ),
          'image:caption' => array(
            '#value' => $file_by_fid->image_caption,
          ),
          'image:license_url' => array(
            '#value' => $file_by_fid->image_license_url,
          ),
        );
      }

      $node_data['#children'] = $node_images;
      $nodes[] = $node_data;
    }

    // Make the node data alterable with a
    // hook_better_google_image_sitemap_sitemap_alter.
    drupal_alter('better_google_image_sitemap_sitemap', $nodes);

    // Now we have all the nodes in the nodes array, assemble it into a chunk
    // of XML.
    $output = theme('better_google_image_sitemap_sitemap', array('nodes' => $nodes));

    // Once done, write the sitemap's database record, being sure to set updated
    // to now, rebuild_needed to false, and the XML to the generated XML.
    db_merge('better_google_image_sitemap_sitemaps')
      ->key(array('language' => $sitemap_to_rebuild))
      ->fields(array(
        'rebuild_needed' => BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NOT_NEEDED,
        'updated' => REQUEST_TIME,
        'xml' => $output,
      ))
      ->execute();

    $counter++;

    // Check if time is up.
    if (!is_null($time_to_finish) && (microtime(TRUE) >= $time_to_finish)) {
      break;
    }
  }

  return $counter;
}

/**
 * Load the sitemap for a language. Uses the current language if none specified.
 *
 * @param string|null $language_id
 *   The two-letter language code. If not provided, uses the current language.
 *
 * @return object|FALSE
 *   The database record for the sitemap, or FALSE if nothing found.
 */
function better_google_image_sitemap_get_sitemap($language_id = NULL) {
  // If no language is provided, use the current language.
  if (is_null($language_id)) {
    // Get the current language.
    global $language;
    $language_id = $language->language;
  }

  $sitemaps = better_google_image_sitemap_get_sitemaps();

  return isset($sitemaps[$language_id]) ? $sitemaps[$language_id] : FALSE;
}

/**
 * Get all sitemaps.
 *
 * @return array
 *   An array of sitemap information where each element is a record object
 *   returned from the {better_google_image_sitemap_sitemaps} database table.
 */
function better_google_image_sitemap_get_sitemaps() {
  $sitemaps = &drupal_static(__FUNCTION__);

  // If we haven't already built this list.
  if (is_null($sitemaps)) {
    // Select bcs.language, bcs.rebuild_needed from
    // {better_google_image_sitemap_sitemaps} bcs where 1.
    $results = db_select('better_google_image_sitemap_sitemaps', 'bcs')
      ->fields('bcs')
      ->execute()
      ->fetchAll();

    $sitemaps = array();

    foreach ($results as $result) {
      $sitemaps[$result->language] = $result;

      // Add in the sitemap URL.
      $sitemaps[$result->language]->url = better_google_image_sitemap_get_sitemap_url($result->language);
    }
  }

  return $sitemaps;
}

/**
 * Gets the absolute URL of the sitemap.
 *
 * @param string $language
 *   The language code for the sitemap to be generated, e.g. "en".
 *
 * @return string
 *   The absolute URL of a sitemap.
 */
function better_google_image_sitemap_get_sitemap_url($language) {
  $languages = language_list();

  return url(BETTER_GOOGLE_IMAGE_SITEMAP_SITEMAP_PATH, array(
    'absolute' => TRUE,
    'language' => $languages[$language],
  ));
}

/**
 * Update the index of images.
 *
 * (Re-)Indexes nodes whose updated timestamp is newer than the last-indexed
 * timestamp (or which haven't yet been indexed).
 *
 * @param string $current_language_id
 *   The language to be indexed. This indexing process will also
 *
 * @param null   $time_to_finish
 *   A timestamp of when we must finish processing by, if this task must not
 *   take longer than a certain amount of time, or NULL if it is allowed to
 *   keep going until it's done.
 *
 * @return int
 *   The number of records processed.
 */
function better_google_image_sitemap_index_images($current_language_id = NULL, $time_to_finish = NULL) {
  // If we haven't been given a language to fetch, use the current site
  // language.
  if (is_null($current_language_id)) {
    // Get the current language.
    global $language;
    $current_language_id = $language->language;
  }

  // Populate empty sitemaps.
  better_google_image_sitemap_create_empty_sitemaps();

  $sitemap_languages = better_google_image_sitemap_get_sitemaps();

  // Get site languages.
  $languages = language_list();

  // Create a counter so we can tell how many records we've processed.
  $counter = 0;

  // Get the database schema.
  $schema = drupal_get_schema();

  // Get a list of database table names which contain data for image fields
  // linked to nodes.
  $query = db_select('field_config_instance', 'fci')
    ->fields('fci', array('field_name'));

  $query->join('field_config', 'fc', 'fci.field_id = fc.id');

  $query->addField('fc', 'data', 'fc_data');

  $results = $query->fields('fc')
    ->condition('fci.entity_type', 'node')
    ->condition('fc.type', 'image')
    ->condition('fc.deleted', 0)
    ->execute()
    ->fetchAll();

  foreach ($results as &$result) {
    $result->fc_data = unserialize($result->fc_data);
  }

  // Get indexable node types for this language.
  $all_indexable_node_types = better_google_image_sitemap_indexable_node_types();

  // Get the array keys of the node types for use in our Massive Query Of Doon.
  $this_lang_indexable_node_types = array_keys($all_indexable_node_types[$current_language_id]);

  // Loop through each result.
  foreach ($results as $result) {
    // Get the table name.
    // If the db schema contains a table matching field_data_{image field name}:
    if (array_key_exists('field_data_' . $result->field_name, $schema)) {
      // Get the list of images from the table whose entity type is node; we
      // need to return entity_id (node ID), language, {field name}_fid, {field
      // name}_alt, {field name}_title.
      $query = db_select('field_data_' . $result->field_name, 'fd')
        ->fields('fd', array(
          'entity_id',
          'language',
        ));

      $query->condition('fd.entity_type', 'node');
      $query->condition('fd.deleted', 0);
      $query->condition('fd.language', array($current_language_id, LANGUAGE_NONE), 'IN');

      $query->addField('fd', $result->field_name . '_fid', 'fid');
      $query->addField('fd', $result->field_name . '_alt', 'alt');
      $query->addField('fd', $result->field_name . '_title', 'title');

      // Join on the file-managed table by fid.
      $query->join('file_managed', 'fm', 'fm.fid = fd.' . $result->field_name . '_fid');

      // Get the image filename and URI from the file_managed table.
      $query->addField('fm', 'uri');
      $query->addField('fm', 'filename');

      // Filter by type=image and filemime like image/%
      $query->condition('fm.type', 'image');
      $query->condition('fm.filemime', array(
        'image/png',
        'image/jpg',
        'image/gif',
        'image/jpeg',
      ), 'IN');

      // Join on the node table.
      $query->join('node', 'n', 'n.nid = fd.entity_id');

      // Only allow indexing of indexable node types for this language.
      $query->condition('n.type', array($this_lang_indexable_node_types), 'IN');

      // Require a published node.
      $query->condition('n.status', NODE_PUBLISHED);

      // Get the last-changed timestamp from the node table.
      $query->addField('n', 'changed', 'n_changed');

      // Use a 'node_access' tag with a query to find out how many this user has
      // access to. This is the standard way to make lists while respecting
      // node access restrictions.
      $query->addTag('node_access');

      // Get some more information about the related node.
      $query->addField('n', 'nid');
      $query->addField('n', 'type', 'n_type');
      $query->addField('n', 'changed', 'changed');
      $query->addField('n', 'title', 'node_title');

      // Find out if the image has been indexed already, and when; bci_updated
      // will be null if not indexed - otherwise, it will indicate the
      // timestamp of the last index.
      $query->leftJoin('better_google_image_sitemap_images', 'bci', 'fm.fid = bci.fid');
      $query->addField('bci', 'updated', 'bci_updated');

      // Only get images attached to nodes which are marked as needing update.
      $or = db_or()->condition('bci.updated', NULL)->condition('bci.updated', 'n.changed', '<');
      $query->condition($or);

      $files = $query->execute()->fetchAll();

      // Update the images index.
      foreach ($files as $file) {
        // If $file->language is LANGUAGE_NONE, set it to the current
        // language.
        $file_language = ($file->language == LANGUAGE_NONE) ? $current_language_id : $file->language;

        db_delete('better_google_image_sitemap_images')
          ->condition('fid', $file->fid)
          ->condition('nid', $file->nid)
          ->condition('language', $file->language)
          ->execute();

        // Remove hyphens and underscores from the filename.
        $filename = str_replace(array('-', '_'), ' ', $file->filename);
        $filename = ucwords($filename);

        // Remove the extension from the filename.
        $filename = preg_replace('/\\.[^.\\s]{3,4}$/', '', $filename);

        // Get the image title from the file's alt text, falling back to
        // filename (with hyphens and underscores replaced by spaces, and the
        // first letters of each word capitalised.
        $image_title = trim($file->alt);
        if (empty($image_title)) {
          $image_title = $filename;
        }

        // Do the same for caption; if there's no caption, use the image alt if
        // available first.
        $image_caption = trim($file->title);
        if (empty($image_caption)) {
          $image_caption = $image_title;
        }

        db_insert('better_google_image_sitemap_images')
          ->fields(array(
            'fid' => $file->fid,
            'nid' => $file->nid,
            'language' => $file_language,
            'node_url' => url('node/' . $file->nid, array(
              'absolute' => TRUE,
              'language' => $languages[$file_language],
            )),
            'image_url' => file_create_url($file->uri),
            'image_title' => check_plain($image_title),
            'image_caption' => check_plain($image_caption),
            'image_license_url' => variable_get('better_google_image_sitemap_settings_image_license_url'),
            'updated' => REQUEST_TIME,
          ))
          ->execute();

        // Increment our counter to indicate we've done A Thing.
        $counter++;

        // If the file's node's language is for a site map which isn't yet
        // flagged to be rebuilt:
        // drupal_write_record set rebuild_needed = TRUE where language = {file
        // node's language}.
        if (!$sitemap_languages[$current_language_id]->rebuild_needed) {
          db_update('better_google_image_sitemap_sitemaps')
            ->condition('language', $current_language_id)
            ->fields(array(
              'rebuild_needed' => BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NEEDED,
            ))
            ->execute();

          // Set the flag so we don't run this query unnecessarily.
          $sitemap_languages[$current_language_id] = TRUE;
        }

        // Check if time is up.
        if (!is_null($time_to_finish) && (microtime(TRUE) >= $time_to_finish)) {
          break;
        }
      }
    }
  }

  // Return the number of indexed images.
  return $counter;
}

/**
 * Get an array of indexable node types per language.
 *
 * @return array
 *   An associative array of node types which can be indexed for a language.
 *   The array key is the language code, e.g. "en".
 */
function better_google_image_sitemap_indexable_node_types() {
  $indexable_node_types = array();

  // Get a list of languages.
  $languages = language_list();

  foreach ($languages as $language_id => $language_info) {
    $indexable_node_types[$language_id] = variable_get('better_google_image_sitemap_settings_language_' . $language_id,
      better_google_image_sitemap_settings_language_node_type_defaults());
  }

  // Implement a hook_better_google_image_sitemap_indexable_node_types_alter.
  drupal_alter('better_google_image_sitemap_indexable_node_types', $indexable_node_types);

  return $indexable_node_types;
}

/**
 * Get a list of node types and turn into an array for the module's options.
 *
 * @return array
 *   The array of node types.
 */
function better_google_image_sitemap_language_node_types() {
  $node_types = node_type_get_types();

  $language_node_types = array();

  foreach ($node_types as $node_type => $node_type_information) {
    $language_node_types[$node_type] = $node_type_information->name;
  }

  // Sort by the node type name, alphabetically descending. We do this by
  // flipping the array's keys and values, performing a key sort, and then
  // flipping it back. Simples!
  $language_node_types = array_flip($language_node_types);
  ksort($language_node_types);
  $language_node_types = array_flip($language_node_types);

  // Implement a hook_better_google_image_sitemap_language_node_types_alter.
  drupal_alter('better_google_image_sitemap_language_node_types', $language_node_types);

  return $language_node_types;
}

/**
 * Determine whether a particular node type is allowed to be indexed.
 *
 * @param string $node_type
 *   The node type.
 *
 * @param string $language
 *   The language code.
 *
 * @return bool
 *   TRUE if the node type is indexable, or FALSE if computer says no.
 */
function better_google_image_sitemap_node_type_indexable($node_type, $language) {
  $indexable_node_types = better_google_image_sitemap_indexable_node_types();

  return array_key_exists($node_type, $indexable_node_types[$language]);
}

/**
 * Settings form which configures the Images Sitemap module.
 *
 * @param array $form
 *   A FAPI-compatible form array.
 *
 * @param array $form_state
 *   The form state array.
 *
 * @return array
 *   A Drupal system settings form.
 */
function better_google_image_sitemap_settings($form, &$form_state) {
  // Allow configuration of the maximum time each cron indexing run should be
  // allowed to run for.
  $form['better_google_image_sitemap_settings_cron_max_execution_time'] = array(
    '#type' => 'select',
    '#title' => t('Maximum amount of time per cron run to index nodes?'),
    '#description' => t('Set the time limit to limit the length of each indexing run. You should set this to less than the maximum PHP execution time for cron runs; this usually depends on how cron is run on your system, but 30 seconds is a reasonably safe bet.'),
    '#default_value' => variable_get('better_google_image_sitemap_settings_cron_max_execution_time', 30),
    '#options' => array(
      15 => t('15 seconds'),
      30 => t('30 seconds (recommended)'),
      60 => t('1 minute'),
      60 * 2 => t('2 minutes'),
      60 * 5 => t('5 minutes'),
      0 => t('No limit (not recommended)'),
    ),
    '#required' => TRUE,
  );

  // Do we want to include images from new content types automagically?
  $form['better_google_image_sitemap_settings_include_new_content_types_by_default'] = array(
    '#type' => 'checkbox',
    '#title' => t('Include images from new content types by default?'),
    '#description' => t('Do you want this module to automatically include images from newly-created content types (for example, when a module is installed which provides its own content type), or do you want new content types to be excluded until you explicitly include them?'),
    '#default_value' => variable_get(
      'better_google_image_sitemap_settings_include_new_content_types_by_default',
      BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_NEW_NODES_BY_DEFAULT),
  );

  // Get a list of friendly names of search engines.
  $search_engines_list = array();

  foreach (better_google_image_sitemap_search_engine_info() as $search_engine) {
    $search_engines_list[] = l($search_engine['name'],
      $search_engine['help url'], array('attributes' => array('target' => '_blank')));
  }

  // Do we want to submit image sitemaps to search engines automagically? It's
  // unlikely we would want to turn this option off, unless we were in a
  // development environment.
  $form['better_google_image_sitemap_settings_submit_on_cron'] = array(
    '#type' => 'checkbox',
    '#title' => t('Submit image sitemaps to search engines automagically?'),
    '#description' => t('Image sitemaps will be submitted to <strong>!search_engines_list</strong>. You should keep this option enabled for production sites, unless you wish to handle the submission of image sitemaps elsewhere. You should also turn this option off on development sites.', array(
      '!search_engines_list' => implode(', ', $search_engines_list),
    )),
    '#default_value' => variable_get(
      'better_google_image_sitemap_settings_submit_on_cron',
      BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_ON_CRON_BY_DEFAULT),
  );

  // What is the URL of the license under which the images are published?
  $form['better_google_image_sitemap_settings_image_license_url'] = array(
    '#type' => 'textfield',
    '#title' => t('Image license information URL'),
    '#description' => t('What is the URL of the license under which the images are published?'),
    '#default_value' => variable_get('better_google_image_sitemap_settings_image_license_url'),
    '#maxlength' => 255,
  );

  // Get a list of node types and turn into an array for the module's options.
  $language_node_types = better_google_image_sitemap_language_node_types();

  // Get all site languages and build configuration options for each.
  $languages = language_list();

  foreach ($languages as $language_id => $language) {
    $form['better_google_image_sitemap_settings_language_' . $language_id] = array(
      '#type' => 'checkboxes',
      '#title' => $language->name,
      '#description' => t('Tick the checkboxes for the content types whose images you would like to include in the sitemap for the @language_name language.',
        array('@language_name' => $language->name)),
      '#default_value' => variable_get(
        'better_google_image_sitemap_settings_language_' . $language_id,
        better_google_image_sitemap_settings_language_node_type_defaults(TRUE)),
      '#options' => $language_node_types,
    );
  }

  // Turn this into a settings form.
  return system_settings_form($form);
}

/**
 * Loads indexing defaults for node types.
 *
 * Creates an array of node types and a boolean to indicate whether their
 * content should be included in the site map or not.
 *
 * @return array
 *   An array with node types as the keys and values; all node types listed in
 *   the array are indexable.
 */
function better_google_image_sitemap_settings_language_node_type_defaults() {
  $language_configuration = array();

  // We only need to assemble a set of default values if we are to include new
  // content types by default.
  if (variable_get('better_google_image_sitemap_settings_include_new_content_types_by_default', TRUE)) {
    // Get a list of node types.
    $node_types = node_type_get_types();

    foreach ($node_types as $node_type => $node_type_information) {
      $language_configuration[$node_type] = $node_type;
    }
  }

  return $language_configuration;
}

/**
 * Gets a list of any sitemaps which require rebuild.
 *
 * @return array
 *   An associative array of sitemaps, where each key and value is the sitemap's
 *   language identifier.
 */
function better_google_image_sitemap_sitemaps_for_rebuild() {
  $sitemaps_to_rebuild = &drupal_static(__FUNCTION__);

  // If we haven't already built this list.
  if (is_null($sitemaps_to_rebuild)) {
    $sitemaps_to_rebuild = array();

    foreach (better_google_image_sitemap_get_sitemaps() as $sitemap) {
      if ($sitemap->rebuild_needed == BETTER_GOOGLE_IMAGE_SITEMAP_REBUILD_NEEDED) {
        $sitemaps_to_rebuild[$sitemap->language] = $sitemap->language;
      }
    }
  }

  return $sitemaps_to_rebuild;
}

/**
 * Provides a form with status information and buttons to rebuild indices.
 *
 * @param array $form
 *   A FAPI-compatible form array.
 *
 * @param array $form_state
 *   The form state array.
 *
 * @return array
 *   A Drupal system settings form.
 */
function better_google_image_sitemap_status_form($form, &$form_state) {
  // Create a summary of the site's indexed images and the sitemaps generated,
  // if any.
  $useful_info = better_google_image_sitemap_useful_info();

  $useful_info_bullet_points = array(
    t('Total images: @images_total', array('@images_total' => $useful_info['images']['images_total'])),
    t('Images which need to be re-indexed: @images_for_reindexing', array('@images_for_reindexing' => $useful_info['images']['images_for_reindexing'])),
    t('Total nodes: @nodes_total', array('@nodes_total' => $useful_info['nodes']['nodes_total'])),
    t('Nodes which need to be re-indexed: @nodes_for_reindexing', array('@nodes_for_reindexing' => $useful_info['nodes']['nodes_for_reindexing'])),
    t('Nodes not in index: @nodes_not_in_index', array('@nodes_not_in_index' => $useful_info['nodes']['nodes_not_in_index'])),
    t('Sitemaps in index: @sitemaps_total', array('@sitemaps_total' => $useful_info['sitemaps']['total'])),
  );

  foreach ($useful_info['sitemaps']['sitemaps'] as $language => $sitemap_info) {
    $useful_info_bullet_points[] = t('Sitemap @language last built @date - status: @status - url: !url', array(
      '@language' => $language,
      '@date' => format_date($sitemap_info['updated']),
      '@status' => $sitemap_info['rebuild_needed'] ? t('rebuild needed') : t('OK'),
      '!url' => l($sitemap_info['url'], $sitemap_info['url']),
      ));
  }

  $form['useless_info'] = array(
    '#type' => 'markup',
    '#markup' => '<h3>' . t('Index Information') . '</h3>
      <ul><li>' . implode('</li> <li>', $useful_info_bullet_points) . '</li></ul>',
  );

  // Create the buttons.
  $form[BETTER_GOOGLE_IMAGE_SITEMAP_FLAG_FOR_REBUILD] = array(
    '#type' => 'submit',
    '#value' => t('1. Flag sitemaps for reindex (does not delete existing sitemaps)'),
    '#name' => BETTER_GOOGLE_IMAGE_SITEMAP_FLAG_FOR_REBUILD,
  );

  $form[BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_IMAGES] = array(
    '#type' => 'submit',
    '#value' => t('2. Index images'),
    '#name' => BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_IMAGES,
  );

  $form[BETTER_GOOGLE_IMAGE_SITEMAP_GENERATE_SITEMAPS] = array(
    '#type' => 'submit',
    '#value' => t('3. Generate sitemaps'),
    '#name' => BETTER_GOOGLE_IMAGE_SITEMAP_GENERATE_SITEMAPS,
  );

  $form[BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_SITEMAPS] = array(
    '#type' => 'submit',
    '#value' => t('4. Submit sitemaps to Google and Bing'),
    '#name' => BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_SITEMAPS,
  );

  return $form;
}

/**
 * Get useful status information about indexed images.
 *
 * @return array
 *   An array of (maybe) useful information.
 */
function better_google_image_sitemap_useful_info() {
  $useful_info = array(
    'images' => array(),
    'nodes' => array(),
    'sitemaps' => array(
      'total' => 0,
      'sitemaps' => array(),
    ),
  );

  // Get a count of the images in the sitemap, and how many are stale.
  $query = db_select('better_google_image_sitemap_images', 'bgi');
  $query->leftJoin('node', 'n', 'n.nid = bgi.nid');

  $useful_info['images']['images_total'] = $query->countQuery()->execute()->fetchField();

  $query = db_select('better_google_image_sitemap_images', 'bgi');
  $query->leftJoin('node', 'n', 'n.nid = bgi.nid');

  // Only get images attached to nodes which were indexed before the last node
  // update.
  $query->condition('bgi.updated', 'n.changed', '<');

  $useful_info['images']['images_for_reindexing'] = $query->countQuery()->execute()->fetchField();

  // Get a count of the number of nodes which are and aren't indexed (i.e. which
  // don't have images, or which haven't been indexed yet).
  $query = db_select('node', 'n');
  $query->leftJoin('better_google_image_sitemap_images', 'bgi', 'n.nid = bgi.nid');

  // Only get images attached to nodes which were indexed before the last node
  // update.
  $query->condition('bgi.updated', 'n.changed', '<');

  $useful_info['nodes']['nodes_for_reindexing'] = $query->countQuery()->execute()->fetchField();

  $query = db_select('node', 'n');
  $query->leftJoin('better_google_image_sitemap_images', 'bgi', 'n.nid = bgi.nid');

  // Only get images attached to nodes which are marked as needing update.
  $query->condition('bgi.updated', NULL);

  $useful_info['nodes']['nodes_not_in_index'] = $query->countQuery()->execute()->fetchField();

  $query = db_select('better_google_image_sitemap_images', 'bgi')
    ->fields('bgi', array('nid'))
    ->distinct();

  $useful_info['nodes']['nodes_total'] = $query->countQuery()->execute()->fetchField();

  // Get details of the sitemap(s) and whether they're flagged as needing a
  // rebuild.
  $sitemaps = better_google_image_sitemap_get_sitemaps();

  $useful_info['sitemaps']['total'] = count($sitemaps);

  foreach ($sitemaps as $sitemap) {
    $sitemap_info = array(
      'updated' => $sitemap->updated,
      'rebuild_needed' => $sitemap->rebuild_needed,
      'url' => $sitemap->url,
    );

    $useful_info['sitemaps']['sitemaps'][$sitemap->language] = $sitemap_info;
  }

  return $useful_info;
}

/**
 * Validation handler for the status form.
 *
 * @param array $form
 *   A FAPI-compatible form array.
 *
 * @param array $form_state
 *   The form state array.
 */
function better_google_image_sitemap_status_form_validate($form, &$form_state) {
}

/**
 * Submit handler for the status form.
 *
 * @param array $form
 *   A FAPI-compatible form array.
 *
 * @param array $form_state
 *   The form state array.
 */
function better_google_image_sitemap_status_form_submit($form, &$form_state) {
  // What function are we running?
  switch ($form_state['triggering_element']['#name']) {
    // Flag everything for rebuild.
    case BETTER_GOOGLE_IMAGE_SITEMAP_FLAG_FOR_REBUILD:
      better_google_image_sitemap_flag_for_rebuild();
      drupal_set_message(t('Flagged all nodes and sitemaps for rebuild; please now run step 2 to reindex images, and step 3 to create the sitemaps.'));

      break;

    // Index the images.
    case BETTER_GOOGLE_IMAGE_SITEMAP_INDEX_IMAGES:
      $images_indexed = 0;

      foreach (language_list() as $language_id => $language) {
        $images_indexed += better_google_image_sitemap_index_images($language_id);
      }

      // Count the total number of images for indexing.
      $query = db_select('better_google_image_sitemap_images', 'bgi')
        ->fields('bgi', array('id'))
        ->execute();

      $count_all = $query->rowCount();

      drupal_set_message(t('Indexed @count images ready for the next sitemap build. @count_all images are in the index.', array(
        '@count' => $images_indexed,
        '@count_all' => $count_all,
      )));

      break;

    // Generate sitemaps.
    case BETTER_GOOGLE_IMAGE_SITEMAP_GENERATE_SITEMAPS:
      $sitemaps_generated = better_google_image_sitemap_generate_sitemaps();

      $sitemap_links_array = array();
      $languages = language_list();

      foreach (better_google_image_sitemap_get_sitemaps() as $sitemap) {
        $sitemap_url = url(BETTER_GOOGLE_IMAGE_SITEMAP_SITEMAP_PATH, array('absolute' => TRUE, 'language' => $languages[$sitemap->language]));
        $sitemap_links_array[] = l($sitemap_url, $sitemap_url);
      }

      // Create the links to the sitemaps, if we have any.
      $sitemap_links = '';
      if (!empty($sitemap_links_array)) {
        $sitemap_links = ' ' . t('The following sitemaps exist:') . ' ' . implode(', ', $sitemap_links_array);
      }

      drupal_set_message(t('Generated @count image sitemap(s).!sitemap_links.', array(
        '@count' => $sitemaps_generated,
        '!sitemap_links' => $sitemap_links,
      )));

      break;

    // Submit sitemaps to Google and Bing.
    case BETTER_GOOGLE_IMAGE_SITEMAP_SUBMIT_SITEMAPS:
      // Submit to search engines.
      $successful_submissions = better_google_image_sitemap_submit_sitemaps();

      // Log to watchdog so we can see what's occurrin'.
      $message_text = 'Successfully submitted @successful_submissions sitemaps to search engines.';
      $message_variables = array(
        '@successful_submissions' => $successful_submissions,
      );

      drupal_set_message(t($message_text, $message_variables));

      break;
  }

  // Once finished, drupal_goto the status page.
  drupal_goto($_GET['q']);
}

/**
 * Menu callback to output a sitemap to the page and then exit.
 */
function better_google_image_sitemap_view_sitemap() {
  drupal_add_http_header('Content-type', 'text/xml');
  echo better_google_image_sitemap_get_sitemap()->xml;
  drupal_page_footer();

  exit;
}

/**
 * Submit the sitemaps to a specific URL, usually during cron.
 *
 * Function stolen shamelessly from the xmlsitemap_engines module :)
 *
 * @return int
 *   A count of the number of sitemaps which were successfully submitted and
 *   received an HTTP 200 response.
 */
function better_google_image_sitemap_submit_sitemaps() {
  // Get the configured sitemap search engines.
  $sitemap_search_engines = better_google_image_sitemap_search_engine_info();
  $sitemaps = better_google_image_sitemap_get_sitemaps();

  // Get languages.
  $languages = language_list();

  // Start a counter of the number of submissions made.
  $counter = 0;

  foreach ($sitemaps as $sitemap) {
    $sitemap_url = url(BETTER_GOOGLE_IMAGE_SITEMAP_SITEMAP_PATH, array(
      'absolute' => TRUE,
      'language' => $languages[$sitemap->language],
    ));

    foreach ($sitemap_search_engines as $sitemap_search_engine) {
      $submit_url = better_google_image_sitemap_prepare_url($sitemap_search_engine['url'], $sitemap_url);
      $request = drupal_http_request($submit_url);

      // If the response code is 200, increment the counter.
      if ($request->code == 200) {
        $counter++;
        $message_type = 'status';
        $watchdog_type = WATCHDOG_NOTICE;
      }
      else {
        $message_type = 'error';
        $watchdog_type = WATCHDOG_ERROR;
      }

      $message_text = 'Submitted the image sitemap for @language to @search_engine at %url and received response @code.';
      $message_variables = array(
        '@language' => $sitemap->language,
        '@search_engine' => $sitemap_search_engine['name'],
        '%url' => $submit_url,
        '@code' => $request->code,
      );

      watchdog('better google image sitemap', $message_text, $message_variables, $watchdog_type);
      drupal_set_message(t($message_text, $message_variables), $message_type);
    }
  }

  return $counter;
}

/**
 * Get information about sitemaps.
 *
 * Shamelessly borrows from xmlsitemap_engines_xmlsitemap_engine_info().
 *
 * @return array
 *   An array of sitemap engines.
 */
function better_google_image_sitemap_search_engine_info() {
  $engines['google'] = array(
    'name' => t('Google'),
    'url' => 'http://www.google.com/webmasters/tools/ping?sitemap=[sitemap]',
    'help url' => 'http://www.google.com/support/webmasters/bin/answer.py?hl=en&answer=156184',
  );

  $engines['bing'] = array(
    'name' => t('Bing'),
    'url' => 'http://www.bing.com/webmaster/ping.aspx?siteMap=[sitemap]',
    'help url' => 'http://www.bing.com/webmaster',
  );

  return $engines;
}

/**
 * Replace valid tokens in the URL with their appropriate values.
 *
 * Shamelessly borrows from xmlsitemap_engines_prepare_url().
 *
 * @param string $search_engine_ping_url
 *   An un-tokenized URL.
 *
 * @param string $sitemap_url
 *   The URL of the sitemap.
 *
 * @return mixed
 *   A tokenized URL.
 */
function better_google_image_sitemap_prepare_url($search_engine_ping_url, $sitemap_url) {
  return str_replace('[sitemap]', $sitemap_url, $search_engine_ping_url);
}
