# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import mock

from dashboard.pinpoint.models import change


class ChangeTest(unittest.TestCase):

  def testChange(self):
    base_commit = change.Dep('chromium/src', 'aaa7336')
    dep = change.Dep('external/github.com/catapult-project/catapult', 'e0a2efb')
    patch = 'rietveld/codereview.chromium.org/2565263002/20001'

    # Also test the deps conversion to tuple.
    c = change.Change(base_commit, [dep], patch)

    self.assertEqual(c, change.Change(base_commit, (dep,), patch))
    string = ('src@aaa7336 catapult@e0a2efb + '
              'rietveld/codereview.chromium.org/2565263002/20001')
    self.assertEqual(str(c), string)
    self.assertEqual(c.base_commit, base_commit)
    self.assertEqual(c.deps, (dep,))
    self.assertEqual(c.all_deps, (base_commit, dep))
    self.assertEqual(c.most_specific_commit, dep)
    self.assertEqual(c.patch, patch)

  @mock.patch('dashboard.services.gitiles_service.CommitRange')
  def testMidpointSuccess(self, commit_range):
    commit_range.return_value = [
        {'commit': 'babe852'},
        {'commit': 'b57345e'},
        {'commit': '949b36d'},
        {'commit': '1ef4789'},
    ]

    change_a = change.Change(change.Dep('chromium/src', '0e57e2b'),
                             (change.Dep('catapult', 'e0a2efb'),))
    change_b = change.Change(change.Dep('chromium/src', 'babe852'),
                             (change.Dep('catapult', 'e0a2efb'),))
    self.assertEqual(change.Change.Midpoint(change_a, change_b),
                     change.Change(change.Dep('chromium/src', '949b36d'),
                                   (change.Dep('catapult', 'e0a2efb'),)))

  def testMidpointRaisesWithDifferingNumberOfDeps(self):
    change_a = change.Change(change.Dep('chromium/src', '0e57e2b'))
    change_b = change.Change(change.Dep('chromium/src', 'babe852'),
                             (change.Dep('catapult', 'e0a2efb'),))
    with self.assertRaises(change.NonLinearError):
      change.Change.Midpoint(change_a, change_b)

  def testMidpointRaisesWithDifferingPatch(self):
    change_a = change.Change(change.Dep('chromium/src', '0e57e2b'))
    change_b = change.Change(change.Dep('chromium/src', 'babe852'),
                             patch='patch')
    with self.assertRaises(change.NonLinearError):
      change.Change.Midpoint(change_a, change_b)

  def testMidpointRaisesWithDifferingRepository(self):
    change_a = change.Change(change.Dep('chromium/src', '0e57e2b'))
    change_b = change.Change(change.Dep('not/chromium/src', 'babe852'))
    with self.assertRaises(change.NonLinearError):
      change.Change.Midpoint(change_a, change_b)

  def testMidpointRaisesWithTheSameChange(self):
    c = change.Change(change.Dep('chromium/src', '0e57e2b'))
    with self.assertRaises(change.NonLinearError):
      change.Change.Midpoint(c, c)

  def testMidpointRaisesWithMultipleDifferingCommits(self):
    change_a = change.Change(change.Dep('chromium/src', '0e57e2b'),
                             (change.Dep('catapult', 'e0a2efb'),))
    change_b = change.Change(change.Dep('chromium/src', 'babe852'),
                             (change.Dep('catapult', 'bfa19de'),))
    with self.assertRaises(change.NonLinearError):
      change.Change.Midpoint(change_a, change_b)

  @mock.patch('dashboard.services.gitiles_service.CommitRange')
  def testMidpointReturnsNoneWithAdjacentCommits(self, commit_range):
    commit_range.return_value = [{'commit': 'b57345e'}]

    change_a = change.Change(change.Dep('chromium/src', '949b36d'))
    change_b = change.Change(change.Dep('chromium/src', 'b57345e'))
    self.assertIsNone(change.Change.Midpoint(change_a, change_b))


class DepTest(unittest.TestCase):

  def testDep(self):
    dep = change.Dep('chromium/src', 'aaa7336')

    self.assertEqual(dep, change.Dep('chromium/src', 'aaa7336'))
    self.assertEqual(str(dep), 'src@aaa7336')
    self.assertEqual(dep.repository, 'chromium/src')
    self.assertEqual(dep.git_hash, 'aaa7336')

  @mock.patch('dashboard.services.gitiles_service.CommitRange')
  def testMidpointSuccess(self, commit_range):
    commit_range.return_value = [
        {'commit': 'babe852'},
        {'commit': 'b57345e'},
        {'commit': '949b36d'},
        {'commit': '1ef4789'},
    ]

    dep_a = change.Dep('chromium/src', '0e57e2b')
    dep_b = change.Dep('chromium/src', 'babe852')
    self.assertEqual(change.Dep.Midpoint(dep_a, dep_b),
                     change.Dep('chromium/src', '949b36d'))

  def testMidpointRaisesWithDifferingRepositories(self):
    dep_a = change.Dep('chromium/src', '0e57e2b')
    dep_b = change.Dep('not/chromium/src', 'babe852')

    with self.assertRaises(ValueError):
      change.Dep.Midpoint(dep_a, dep_b)

  @mock.patch('dashboard.services.gitiles_service.CommitRange')
  def testMidpointReturnsNoneWithAdjacentCommits(self, commit_range):
    commit_range.return_value = [{'commit': 'b57345e'}]

    dep_a = change.Dep('chromium/src', '949b36d')
    dep_b = change.Dep('chromium/src', 'b57345e')
    self.assertIsNone(change.Dep.Midpoint(dep_a, dep_b))

  @mock.patch('dashboard.services.gitiles_service.CommitRange')
  def testMidpointReturnsNoneWithEmptyRange(self, commit_range):
    commit_range.return_value = []

    dep_b = change.Dep('chromium/src', 'b57345e')
    dep_a = change.Dep('chromium/src', '949b36d')
    self.assertIsNone(change.Dep.Midpoint(dep_a, dep_b))
