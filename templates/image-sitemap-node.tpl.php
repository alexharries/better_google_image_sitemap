<?php

/**
 * @file
 * Themes the output of a single node for an image sitemap.
 */

?>

<url>
  <loc><?php print $node['loc']['#value'] ?></loc>

  <?php foreach (element_children($node['#children']) as $node_child): ?>
    <?php print theme('better_google_image_sitemap_image', array('image' => $node['#children'][$node_child])) ?>
  <?php endforeach ?>
</url>
