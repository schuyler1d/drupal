<?php
// $Id: field.form.inc,v 1.3 2009-02-10 03:16:14 webchick Exp $

/**
 * @file
 * Field forms management.
 */

// TODO : merge with field.default.inc ?

/**
 * Create a separate form element for each field.
 */
function field_default_form($obj_type, $object, $field, $instance, $items, &$form, &$form_state, $get_delta = NULL) {
  // This could be called with no object, as when a UI module creates a
  // dummy form to set default values.
  if ($object) {
    list($id, ,) = field_attach_extract_ids($obj_type, $object);
  }
  $addition = array();

  $field_name = $field['field_name'];

  // If the field is not accessible, don't add anything. The field value will
  // be left unchanged on update, or considered empty on insert.
  // TODO : if/when field_attach_insert() takes care of default values,
  // unaccessible fields will automatically get the default value on insert.
  if (!field_access('edit', $field)) {
    return $addition;
  }

  // Put field information at the top of the form, so that it can be easily
  // retrieved.
  // Note : widgets and other form handling code should *always* fetch
  // field and instance information from $form['#fields'] rather than from
  // field_info_field(). This lets us build forms for 'variants' of a field,
  // for instance on admin screens.
  $form['#fields'][$field_name] = array(
    'field' => $field,
    'instance' => $instance,
  );
  // TODO : why do we need this ?
  $form['#cache'] = FALSE;

  // Populate widgets with default values if we're creating a new object.
  if (empty($items) && empty($id) && !empty($instance['default_value_function'])) {
    $items = array();
    $function = $instance['default_value_function'];
    if (drupal_function_exists($function)) {
      $items = $function($obj_type, $object, $field, $instance);
    }
  }

  $form_element = array();

  // If field module handles multiple values for this form element,
  // and we are displaying an individual element, process the multiple value
  // form.
  if (!isset($get_delta) && field_behaviors_widget('multiple values', $instance) == FIELD_BEHAVIOR_DEFAULT) {
    $form_element = field_multiple_value_form($field, $instance, $items, $form, $form_state);
  }
  // If the widget is handling multiple values (e.g Options),
  // or if we are displaying an individual element, just get a single form
  // element and make it the $delta value.
  else {
    $delta = isset($get_delta) ? $get_delta : 0;
    $function = $instance['widget']['module'] . '_field_widget';
    if (drupal_function_exists($function)) {
      if ($element = $function($form, $form_state, $field, $instance, $items, $delta)) {
        $defaults = array(
          '#required' => $get_delta > 0 ? FALSE : $instance['required'],
          '#columns'  => array_keys($field['columns']),
          '#title' => check_plain(t($instance['label'])),
          '#description' => field_filter_xss($instance['description']),
          '#delta' => $delta,
          '#field_name' => $field['field_name'],
          '#bundle' => $instance['bundle'],
        );
        $element = array_merge($element, $defaults);
        // If we're processing a specific delta value for a field where the
        // field module handles multiples, set the delta in the result.
        // For fields that handle their own processing, we can't make assumptions
        // about how the field is structured, just merge in the returned value.
        if (field_behaviors_widget('multiple values', $instance) == FIELD_BEHAVIOR_DEFAULT) {
          $form_element[$delta] = $element;
        }
        else {
          $form_element = $element;
        }
      }
    }
  }

  if ($form_element) {
    $defaults = array(
      '#field_name' => $field['field_name'],
      '#tree' => TRUE,
      '#weight' => $instance['weight'],
    );

    $addition[$field['field_name']] = array_merge($form_element, $defaults);
    $form['#fields'][$field['field_name']]['form_path'] = array($field['field_name']);
  }

  return $addition;
}

/**
 * Special handling to create form elements for multiple values.
 *
 * Handles generic features for multiple fields:
 * - number of widgets
 * - AHAH-'add more' button
 * - drag-n-drop value reordering
 */
function field_multiple_value_form($field, $instance, $items, &$form, &$form_state) {
  $field = field_info_field($instance['field_name']);
  $field_name = $field['field_name'];

  switch ($field['cardinality']) {
    case FIELD_CARDINALITY_UNLIMITED:
      $filled_items = field_set_empty($field, $items);
      $current_item_count = isset($form_state['field_item_count'][$field_name])
                            ? $form_state['field_item_count'][$field_name]
                            : count($items);
      // We always want at least one empty icon for the user to fill in.
      $max = ($current_item_count > count($filled_items))
              ? $current_item_count - 1
              : $current_item_count;

      break;
    default:
      $max = $field['cardinality'] - 1;
      break;
  }

  $title = check_plain(t($instance['label']));
  $description = field_filter_xss(t($instance['description']));

  $form_element = array(
    '#theme' => 'field_multiple_value_form',
    '#multiple' => $field['cardinality'],
    '#title' => $title,
    '#required' => $instance['required'],
    '#description' => $description,
  );

  $function = $instance['widget']['module'] . '_field_widget';
  if (drupal_function_exists($function)) {
    for ($delta = 0; $delta <= $max; $delta++) {
      if ($element = $function($form, $form_state, $field, $instance, $items, $delta)) {
        $multiple = $field['cardinality'] > 1 || $field['cardinality'] == FIELD_CARDINALITY_UNLIMITED;
        $defaults = array(
          '#title' => $multiple ? '' : $title,
          '#description' => $multiple ? '' : $description,
          '#required' => $delta == 0 && $instance['required'],
          '#weight' => $delta,
          '#delta' => $delta,
          '#columns' => array_keys($field['columns']),
          '#field_name' => $field_name,
          '#bundle' => $instance['bundle'],
        );

        // Add an input field for the delta (drag-n-drop reordering), which will
        // be hidden by tabledrag js behavior.
        if ($multiple) {
          // We name the element '_weight' to avoid clashing with column names
          // defined by field modules.
          $element['_weight'] = array(
            '#type' => 'weight',
            '#delta' => $max, // this 'delta' is the 'weight' element's property
            '#default_value' => isset($items[$delta]['_weight']) ? $items[$delta]['_weight'] : $delta,
            '#weight' => 100,
          );
        }

        $form_element[$delta] = array_merge($element, $defaults);
      }
    }

    // Add AHAH add more button, if not working with a programmed form.
    if ($field['cardinality'] == FIELD_CARDINALITY_UNLIMITED && empty($form['#programmed'])) {
      // Make sure the form is cached so ahah can work.
      $form['#cache'] = TRUE;
      $bundle_name_url_str = str_replace('_', '-', $instance['bundle']);
      $field_name_url_str = str_replace('_', '-', $field_name);

      $form_element[$field_name . '_add_more'] = array(
        '#type' => 'submit',
        '#name' => $field_name . '_add_more',
        '#value' => t('Add another item'),
        '#weight' => $instance['weight'] + $max + 1,
        // Submit callback for disabled JavaScript.
        '#submit' => array('field_add_more_submit'),
        '#ahah' => array(
          'path' => 'field/js_add_more/' . $bundle_name_url_str . '/' . $field_name_url_str,
          'wrapper' => $field_name_url_str . '-items',
          'method' => 'replace',
          'effect' => 'fade',
        ),
        // When JS is disabled, the field_add_more_submit handler will find
        // the relevant field using these entries.
        '#field_name' => $field_name,
        '#bundle' => $instance['bundle'],
      );

      // Add wrappers for the fields and 'more' button.
      $form_element['#prefix'] = '<div class="clear-block" id="' . $field_name_url_str . '-add-more-wrapper"><div id="' . $field_name_url_str . '-items">';
      $form_element[$field_name . '_add_more']['#prefix'] = '<div class="field-add-more">';
      $form_element[$field_name . '_add_more']['#suffix'] =  '</div></div></div>';
    }
  }
  return $form_element;
}

/**
 * Theme an individual form element.
 *
 * Combine multiple values into a table with drag-n-drop reordering.
 * TODO : convert to a template.
 */
function theme_field_multiple_value_form($element) {
  $output = '';

  if ($element['#multiple'] > 1 || $element['#multiple'] == FIELD_CARDINALITY_UNLIMITED) {
    $table_id = $element['#field_name'] . '_values';
    $order_class = $element['#field_name'] . '-delta-order';
    $required = !empty($element['#required']) ? '<span class="form-required" title="' . t('This field is required. ') . '">*</span>' : '';

    $header = array(
      array(
        'data' => t('!title: !required', array('!title' => $element['#title'], '!required' => $required)),
        'colspan' => 2
      ),
      t('Order'),
    );
    $rows = array();

    // Sort items according to '_weight' (needed when the form comes back after
    // preview or failed validation)
    $items = array();
    foreach (element_children($element) as $key) {
      if ($key !== $element['#field_name'] . '_add_more') {
        $items[] = &$element[$key];
      }
    }
    usort($items, '_field_sort_items_value_helper');

    // Add the items as table rows.
    foreach ($items as $key => $item) {
      $item['_weight']['#attributes']['class'] = $order_class;
      $delta_element = drupal_render($item['_weight']);
      $cells = array(
        array('data' => '', 'class' => 'field-multiple-drag'),
        drupal_render($item),
        array('data' => $delta_element, 'class' => 'delta-order'),
      );
      $rows[] = array(
        'data' => $cells,
        'class' => 'draggable',
      );
    }

    $output .= theme('table', $header, $rows, array('id' => $table_id, 'class' => 'field-multiple-table'));
    $output .= $element['#description'] ? '<div class="description">' . $element['#description'] . '</div>' : '';
    $output .= drupal_render($element[$element['#field_name'] . '_add_more']);

    drupal_add_tabledrag($table_id, 'order', 'sibling', $order_class);
  }
  else {
    foreach (element_children($element) as $key) {
      $output .= drupal_render($element[$key]);
    }
  }

  return $output;
}

/**
 * Submit handler to add more choices to a field form. This handler is used when
 * JavaScript is not available. It makes changes to the form state and the
 * entire form is rebuilt during the page reload.
 */
function field_add_more_submit($form, &$form_state) {
  // Set the form to rebuild and run submit handlers.
  if (isset($form['#builder_function']) && drupal_function_exists($form['#builder_function'])) {
    $form['#builder_function']($form, $form_state);

    // Make the changes we want to the form state.
    $field_name = $form_state['clicked_button']['#field_name'];
    if ($form_state['values'][$field_name][$field_name . '_add_more']) {
      $form_state['field_item_count'][$field_name] = count($form_state['values'][$field_name]);
    }
  }
}

/**
 * Menu callback for AHAH addition of new empty widgets.
 */
function field_add_more_js($bundle_name, $field_name) {
  // Arguments are coming from the url, so we translate back dashes.
  $field_name = str_replace('-', '_', $field_name);

  $invalid = FALSE;
  if (empty($_POST['form_build_id'])) {
    // Invalid request.
    $invalid = TRUE;
  }

  // Retrieve the cached form.
  $form_state = array('submitted' => FALSE);
  $form_build_id = $_POST['form_build_id'];
  $form = form_get_cache($form_build_id, $form_state);
  if (!$form) {
    // Invalid form_build_id.
    $invalid = TRUE;
  }

  // Retrieve field information.
  $field = $form['#fields'][$field_name]['field'];
  $instance = $form['#fields'][$field_name]['instance'];
  $form_path = $form['#fields'][$field_name]['form_path'];
  if ($field['cardinality'] != FIELD_CARDINALITY_UNLIMITED) {
    // Ivnalid
    $invalid = TRUE;
  }

  if ($invalid) {
    drupal_json(array('data' => ''));
    exit;
  }

  // We don't simply return a new empty widget row to append to existing ones,
  // because:
  // - ahah.js won't simply let us add a new row to a table
  // - attaching the 'draggable' behavior won't be easy
  // So we resort to rebuilding the whole table of widgets including the
  // existing ones, which makes us jump through a few hoops.

  // The form that we get from the cache is unbuilt. We need to build it so
  // that _value callbacks can be executed and $form_state['values'] populated.
  // We only want to affect $form_state['values'], not the $form itself
  // (built forms aren't supposed to enter the cache) nor the rest of
  // $form_state, so we use copies of $form and $form_state.
  $form_copy = $form;
  $form_state_copy = $form_state;
  $form_copy['#post'] = array();
  form_builder($_POST['form_id'], $form_copy, $form_state_copy);
  // Just grab the data we need.
  $form_state['values'] = $form_state_copy['values'];
  // Reset cached ids, so that they don't affect the actual form we output.
  form_clean_id(NULL, TRUE);

  // Sort the $form_state['values'] we just built *and* the incoming $_POST data
  // according to d-n-d reordering.
  unset($form_state['values'][$field_name][$field['field_name'] . '_add_more']);
  foreach ($_POST[$field_name] as $delta => $item) {
    $form_state['values'][$field_name][$delta]['_weight'] = $item['_weight'];
  }
  $form_state['values'][$field_name] = _field_sort_items($field, $form_state['values'][$field_name]);
  $_POST[$field_name]                = _field_sort_items($field, $_POST[$field_name]);

  // Build our new form element for the whole field, asking for one more element.
  $form_state['field_item_count'] = array($field_name => count($_POST[$field_name]) + 1);
  $items = $form_state['values'][$field_name];
  $form_element = field_default_form(NULL, NULL, $field, $instance, $items, $form, $form_state);
  // Let other modules alter it.
  drupal_alter('form', $form_element, array(), 'field_add_more_js');

  // Add the new element at the right location in the (original, unbuilt) form.
  $target = &$form;
  foreach ($form_path as $key) {
    $target = &$target[$key];
  }
  $target = $form_element[$field_name];

  // Save the new definition of the form.
  $form_state['values'] = array();
  form_set_cache($form_build_id, $form, $form_state);

  // Build the new form against the incoming $_POST values so that we can
  // render the new element.
  $delta = max(array_keys($_POST[$field_name])) + 1;
  $_POST[$field_name][$delta]['_weight'] = $delta;
  $form_state = array('submitted' => FALSE);
  $form += array(
    '#post' => $_POST,
    '#programmed' => FALSE,
  );
  $form = form_builder($_POST['form_id'], $form, $form_state);

  // Render the new output.
  // We get fetch the form element from the built $form.
  $field_form = $form;
  foreach ($form_path as $key) {
    $field_form = $field_form[$key];
  }
  // We add a div around the new field to receive the ahah effect.
  $field_form[$delta]['#prefix'] = '<div class="ahah-new-field">' . (isset($field_form[$delta]['#prefix']) ? $field_form[$delta]['#prefix'] : '');
  $field_form[$delta]['#suffix'] = (isset($field_form[$delta]['#suffix']) ? $field_form[$delta]['#suffix'] : '') . '</div>';
  // TODO : this causes duplication of the wrapping divs

  // If a newly inserted widget contains AHAH behaviors, they normally won't
  // work because AHAH doesn't know about those - it just attaches to the exact
  // form elements that were initially specified in the Drupal.settings object.
  // The new ones didn't exist then, so we need to update Drupal.settings
  // by ourselves in order to let AHAH know about those new form elements.
  $javascript = drupal_add_js(NULL, NULL);
  $output_js = isset($javascript['setting']) ? '<script type="text/javascript">jQuery.extend(Drupal.settings, ' . drupal_to_js(call_user_func_array('array_merge_recursive', $javascript['setting'])) . ');</script>' : '';

  $output = theme('status_messages') . drupal_render($field_form) . $output_js;
  drupal_json(array('status' => TRUE, 'data' => $output));
  exit;
}
