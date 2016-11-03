<?php

/**
 * @file
 * An entire image sitemap.
 */

?><?php print $xml_header ?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"
        xmlns:image="http://www.google.com/schemas/sitemap-image/1.1">

  <?php foreach ($nodes as $node): ?>
    <?php print theme('better_google_image_sitemap_node', array('node' => $node)) ?>
  <?php endforeach ?>

  </urlset>
