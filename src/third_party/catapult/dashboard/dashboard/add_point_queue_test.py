# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from dashboard import add_point_queue
from dashboard.common import testing_common
from dashboard.common import utils
from dashboard.models import graph_data
from dashboard.models import stoppage_alert


class GetOrCreateAncestorsTest(testing_common.TestCase):

  def setUp(self):
    super(GetOrCreateAncestorsTest, self).setUp()
    self.SetCurrentUser('foo@bar.com', is_admin=True)

  def testGetOrCreateAncestors_GetsExistingEntities(self):
    master_key = graph_data.Master(id='ChromiumPerf', parent=None).put()
    graph_data.Bot(id='win7', parent=master_key).put()
    graph_data.TestMetadata(id='ChromiumPerf/win7/dromaeo',).put()
    graph_data.TestMetadata(id='ChromiumPerf/win7/dromaeo/dom').put()
    graph_data.TestMetadata(id='ChromiumPerf/win7/dromaeo/dom/modify').put()
    actual_parent = add_point_queue._GetOrCreateAncestors(
        'ChromiumPerf', 'win7', 'dromaeo/dom/modify')
    self.assertEqual(
        'ChromiumPerf/win7/dromaeo/dom/modify', actual_parent.key.id())
    # No extra TestMetadata or Bot objects should have been added to the
    # database beyond the four that were put in before the _GetOrCreateAncestors
    # call.
    self.assertEqual(1, len(graph_data.Master.query().fetch()))
    self.assertEqual(1, len(graph_data.Bot.query().fetch()))
    self.assertEqual(3, len(graph_data.TestMetadata.query().fetch()))

  def testGetOrCreateAncestors_CreatesAllExpectedEntities(self):
    parent = add_point_queue._GetOrCreateAncestors(
        'ChromiumPerf', 'win7', 'dromaeo/dom/modify')
    self.assertEqual('ChromiumPerf/win7/dromaeo/dom/modify', parent.key.id())
    # Check that all the Bot and TestMetadata entities were correctly added.
    created_masters = graph_data.Master.query().fetch()
    created_bots = graph_data.Bot.query().fetch()
    created_tests = graph_data.TestMetadata.query().fetch()
    self.assertEqual(1, len(created_masters))
    self.assertEqual(1, len(created_bots))
    self.assertEqual(3, len(created_tests))
    self.assertEqual('ChromiumPerf', created_masters[0].key.id())
    self.assertIsNone(created_masters[0].key.parent())
    self.assertEqual('win7', created_bots[0].key.id())
    self.assertEqual('ChromiumPerf', created_bots[0].key.parent().id())
    self.assertEqual('ChromiumPerf/win7/dromaeo', created_tests[0].key.id())
    self.assertIsNone(created_tests[0].parent_test)
    self.assertEqual('win7', created_tests[0].bot_name)
    self.assertEqual('dom', created_tests[1].test_part1_name)
    self.assertEqual(
        'ChromiumPerf/win7/dromaeo', created_tests[1].parent_test.id())
    self.assertIsNone(created_tests[1].bot)
    self.assertEqual(
        'ChromiumPerf/win7/dromaeo/dom/modify', created_tests[2].key.id())
    self.assertEqual(
        'ChromiumPerf/win7/dromaeo/dom', created_tests[2].parent_test.id())
    self.assertIsNone(created_tests[2].bot)

  def testGetOrCreateAncestors_UpdatesStoppageAlert(self):
    testing_common.AddTests(['M'], ['b'], {'suite': {'foo': {}}})
    row = testing_common.AddRows('M/b/suite/foo', {123})[0]
    test = utils.TestKey('M/b/suite/foo').get()
    alert_key = stoppage_alert.CreateStoppageAlert(test, row).put()
    test.stoppage_alert = alert_key
    test.put()
    add_point_queue._GetOrCreateAncestors('M', 'b', 'suite/foo')
    self.assertIsNone(test.key.get().stoppage_alert)
    self.assertTrue(alert_key.get().recovered)
    self.assertIsNotNone(alert_key.get().last_row_timestamp)


if __name__ == '__main__':
  unittest.main()
