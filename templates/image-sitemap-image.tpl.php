<?php

/**
 * @file
 * Themes the output of a single image for an image sitemap.
 */

?>

<image:image>
  <image:loc><?php print $image['image:loc']['#value'] ?></image:loc>
  <?php if (!empty($image['image:title']['#value'])): ?><image:title><?php print $image['image:title']['#value'] ?></image:title><?php endif ?>
  <?php if (!empty($image['image:caption']['#value'])): ?><image:caption><?php print $image['image:caption']['#value'] ?></image:caption><?php endif ?>
  <?php if (!empty($image['image:license_url']['#value'])): ?><image:license><?php print $image['image:license_url']['#value'] ?></image:license><?php endif ?>
</image:image>
