#!/usr/bin/python
# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate, a trusted root, and a target
certificate that is not a CA, and yet has a pathlen set. Verification is
expected to fail, since pathlen should only be set for CAs."""

import common

# Self-signed root certificate (used as trust anchor).
root = common.create_self_signed_root_certificate('Root')

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)

# Target certificate (end entity, but has pathlen set).
target = common.create_end_entity_certificate('Target', intermediate)
target.get_extensions().set_property('basicConstraints',
                                     'critical,CA:false,pathlen:1')


chain = [target, intermediate]
trusted = common.TrustAnchor(root, constrained=False)
time = common.DEFAULT_TIME
key_purpose = common.DEFAULT_KEY_PURPOSE
verify_result = False
errors = """----- Certificate i=0 (CN=Target) -----
ERROR: Target certificate looks like a CA but does not set all CA properties

"""

common.write_test_file(__doc__, chain, trusted, time, key_purpose,
                       verify_result, errors)
