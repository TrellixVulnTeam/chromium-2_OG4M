// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/declarative_event.h"

#include <memory>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "extensions/common/extension_api.h"
#include "extensions/renderer/api_binding_test.h"
#include "extensions/renderer/api_binding_test_util.h"
#include "extensions/renderer/api_bindings_system.h"
#include "extensions/renderer/api_bindings_system_unittest.h"
#include "extensions/renderer/api_last_error.h"
#include "extensions/renderer/api_request_handler.h"
#include "extensions/renderer/api_type_reference_map.h"
#include "extensions/renderer/argument_spec.h"
#include "gin/handle.h"

namespace extensions {

namespace {

const char kDeclarativeAPIName[] = "alpha";
const char kDeclarativeAPISpec[] =
    "{"
    "  'types': [{"
    "    'id': 'alpha.objRef',"
    "    'type': 'object',"
    "    'properties': {"
    "      'prop1': {'type': 'string'},"
    "      'prop2': {'type': 'integer', 'optional': true}"
    "    }"
    "  }, {"
    "    'id': 'alpha.enumRef',"
    "    'type': 'string',"
    "    'enum': ['cat', 'dog']"
    "  }],"
    "  'events': [{"
    "    'name': 'declarativeEvent',"
    "    'options': {"
    "      'supportsRules': true,"
    "      'supportsListeners': false,"
    "      'actions': ['alpha.enumRef'],"
    "      'conditions': ['alpha.objRef']"
    "    }"
    "  }]"
    "}";

bool AllowAllAPIs(const std::string& name) {
  return true;
}

}  // namespace

class DeclarativeEventTest : public APIBindingTest {
 public:
  DeclarativeEventTest()
      : type_refs_(APITypeReferenceMap::InitializeTypeCallback()) {}
  ~DeclarativeEventTest() override {}

  void OnRequest(std::unique_ptr<APIRequestHandler::Request> request,
                 v8::Local<v8::Context> context) {
    last_request_ = std::move(request);
  }

  APITypeReferenceMap* type_refs() { return &type_refs_; }
  APIRequestHandler* request_handler() { return request_handler_.get(); }
  APIRequestHandler::Request* last_request() { return last_request_.get(); }
  void reset_last_request() { last_request_.reset(); }

 private:
  void SetUp() override {
    APIBindingTest::SetUp();

    {
      auto action1 = base::MakeUnique<ArgumentSpec>(ArgumentType::STRING);
      action1->set_enum_values({"actionA"});
      type_refs_.AddSpec("action1", std::move(action1));
      auto action2 = base::MakeUnique<ArgumentSpec>(ArgumentType::STRING);
      action2->set_enum_values({"actionB"});
      type_refs_.AddSpec("action2", std::move(action2));
    }

    {
      auto condition = base::MakeUnique<ArgumentSpec>(ArgumentType::OBJECT);
      auto prop = base::MakeUnique<ArgumentSpec>(ArgumentType::STRING);
      ArgumentSpec::PropertiesMap props;
      props["url"] = std::move(prop);
      condition->set_properties(std::move(props));
      type_refs_.AddSpec("condition", std::move(condition));
    }

    request_handler_ = base::MakeUnique<APIRequestHandler>(
        base::Bind(&DeclarativeEventTest::OnRequest, base::Unretained(this)),
        base::Bind(&RunFunctionOnGlobalAndIgnoreResult),
        APILastError(APILastError::GetParent()));
  }

  void TearDown() override {
    request_handler_.reset();
    APIBindingTest::TearDown();
  }

  APITypeReferenceMap type_refs_;
  std::unique_ptr<APIRequestHandler> request_handler_;
  std::unique_ptr<APIRequestHandler::Request> last_request_;

  DISALLOW_COPY_AND_ASSIGN(DeclarativeEventTest);
};

// Test that the rules schema behaves properly. This is designed to be more of
// a sanity check than a comprehensive test, since it mostly delegates the logic
// out to ArgumentSpec.
TEST_F(DeclarativeEventTest, TestRulesSchema) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  gin::Handle<DeclarativeEvent> emitter = gin::CreateHandle(
      context->GetIsolate(),
      new DeclarativeEvent("declEvent", type_refs(), request_handler(),
                           {"action1", "action2"}, {"condition"}));

  v8::Local<v8::Value> emitter_value = emitter.ToV8();

  const char kAddRules[] =
      "(function(event) {\n"
      "  var rules = %s;\n"
      "  event.addRules(rules);\n"
      "})";

  {
    const char kGoodRules[] =
        "[{id: 'rule', tags: ['tag'],"
        "  actions: ['actionA'],"
        "  conditions: [{url: 'example.com'}],"
        "  priority: 1}]";
    v8::Local<v8::Function> function =
        FunctionFromString(context, base::StringPrintf(kAddRules, kGoodRules));
    v8::Local<v8::Value> args[] = {emitter_value};
    RunFunctionOnGlobal(function, context, arraysize(args), args);

    EXPECT_TRUE(last_request());
    reset_last_request();
  }

  {
    // Invalid action.
    const char kBadRules1[] =
        "[{id: 'rule', tags: ['tag'],"
        "  actions: ['notAnAction'],"
        "  conditions: [{url: 'example.com'}],"
        "  priority: 1}]";
    // Missing required property: conditions.
    const char kBadRules2[] =
        "[{id: 'rule', tags: ['tag'],"
        "  actions: ['actionA'],"
        "  priority: 1}]";
    // Missing required property: actions.
    const char kBadRules3[] =
        "[{id: 'rule', tags: ['tag'],"
        "  conditions: [{url: 'example.com'}],"
        "  priority: 1}]";
    for (const char* rules : {kBadRules1, kBadRules2, kBadRules3}) {
      v8::Local<v8::Function> function =
          FunctionFromString(context, base::StringPrintf(kAddRules, rules));
      v8::Local<v8::Value> args[] = {emitter_value};
      RunFunctionAndExpectError(function, context, arraysize(args), args,
                                "Uncaught TypeError: Invalid invocation");
      EXPECT_FALSE(last_request()) << rules;
      reset_last_request();
    }
  }
}

class DeclarativeEventWithSchemaTest : public APIBindingsSystemTest {
 protected:
  DeclarativeEventWithSchemaTest() {}
  ~DeclarativeEventWithSchemaTest() override {}

  std::vector<FakeSpec> GetAPIs() override {
    // events.removeRules and events.getRules are specified in the events.json
    // schema, so we need to load that.
    base::StringPiece events_schema =
        ExtensionAPI::GetSharedInstance()->GetSchemaStringPiece("events");
    return {{kDeclarativeAPIName, kDeclarativeAPISpec},
            {"events", events_schema.data()}};
  }
};

// Test all methods of declarative events.
TEST_F(DeclarativeEventWithSchemaTest, TestAllMethods) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  v8::Local<v8::Object> api = bindings_system()->CreateAPIInstance(
      kDeclarativeAPIName, context, isolate(), base::Bind(&AllowAllAPIs),
      nullptr);
  ASSERT_FALSE(api.IsEmpty());

  v8::Local<v8::Value> declarative_event =
      GetPropertyFromObject(api, context, "declarativeEvent");
  ASSERT_FALSE(declarative_event.IsEmpty());
  ASSERT_TRUE(declarative_event->IsObject());
  v8::Local<v8::Value> add_rules = GetPropertyFromObject(
      declarative_event.As<v8::Object>(), context, "addRules");
  ASSERT_FALSE(add_rules.IsEmpty());
  ASSERT_TRUE(add_rules->IsFunction());

  v8::Local<v8::Value> args[] = {api};

  {
    const char kAddRules[] =
        R"((function(obj) {
              obj.declarativeEvent.addRules(
                  [{
                    id: 'rule',
                    conditions: [{prop1: 'property'}],
                    actions: ['cat'],
                  }]);
             }))";
    v8::Local<v8::Function> add_rules = FunctionFromString(context, kAddRules);
    RunFunctionOnGlobal(add_rules, context, arraysize(args), args);
    ValidateLastRequest("events.addRules",
                        "['alpha.declarativeEvent',0,"
                        "[{'actions':['cat'],"
                        "'conditions':[{'prop1':'property'}],"
                        "'id':'rule'}]]");
    reset_last_request();
  }

  {
    const char kRemoveRules[] =
        "(function(obj) {\n"
        "  obj.declarativeEvent.removeRules(['rule']);\n"
        "})";
    v8::Local<v8::Function> remove_rules =
        FunctionFromString(context, kRemoveRules);
    RunFunctionOnGlobal(remove_rules, context, arraysize(args), args);
    ValidateLastRequest("events.removeRules",
                        "['alpha.declarativeEvent',0,['rule']]");
    reset_last_request();
  }

  {
    const char kGetRules[] =
        "(function(obj) {\n"
        "  obj.declarativeEvent.getRules(function() {});\n"
        "})";
    v8::Local<v8::Function> remove_rules =
        FunctionFromString(context, kGetRules);
    RunFunctionOnGlobal(remove_rules, context, arraysize(args), args);
    ValidateLastRequest("events.getRules", "['alpha.declarativeEvent',0,null]");
    reset_last_request();
  }
}

}  // namespace extensions
