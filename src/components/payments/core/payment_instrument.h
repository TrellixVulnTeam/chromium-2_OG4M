// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CORE_PAYMENT_INSTRUMENT_H_
#define COMPONENTS_PAYMENTS_CORE_PAYMENT_INSTRUMENT_H_

#include <set>
#include <string>

#include "base/macros.h"
#include "base/strings/string16.h"

namespace payments {

// Base class which represents a form of payment in Payment Request.
class PaymentInstrument {
 public:
  // The type of this instrument instance.
  enum class Type { AUTOFILL };

  class Delegate {
   public:
    virtual ~Delegate() {}

    // Should be called with method name (e.g., "visa") and json-serialized
    // stringified details.
    virtual void OnInstrumentDetailsReady(
        const std::string& method_name,
        const std::string& stringified_details) = 0;

    virtual void OnInstrumentDetailsError() = 0;
  };

  virtual ~PaymentInstrument();

  // Will call into the |delegate| (can't be null) on success or error.
  virtual void InvokePaymentApp(Delegate* delegate) = 0;
  // Returns whether the instrument is complete to be used as a payment method
  // without further editing.
  virtual bool IsCompleteForPayment() = 0;
  // Returns whether the instrument is valid for the purposes of responding to
  // canMakePayment.
  virtual bool IsValidForCanMakePayment() = 0;

  const std::string& method_name() const { return method_name_; }
  const base::string16& label() const { return label_; }
  const base::string16& sublabel() const { return sublabel_; }
  int icon_resource_id() const { return icon_resource_id_; }
  Type type() { return type_; }

 protected:
  PaymentInstrument(const std::string& method_name,
                    const base::string16& label,
                    const base::string16& sublabel,
                    int icon_resource_id,
                    Type type);

 private:
  const std::string method_name_;
  const base::string16 label_;
  const base::string16 sublabel_;
  int icon_resource_id_;
  Type type_;

  DISALLOW_COPY_AND_ASSIGN(PaymentInstrument);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CORE_PAYMENT_INSTRUMENT_H_
