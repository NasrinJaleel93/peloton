//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// operator_expression.h
//
// Identification: src/include/expression/function_expression.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "expression/abstract_expression.h"
#include "common/sql_node_visitor.h"
#include "type/value.h"
#include "catalog/function_catalog.h"

namespace peloton {
namespace expression {

//===----------------------------------------------------------------------===//
// OperatorExpression
//===----------------------------------------------------------------------===//

class FunctionExpression : public AbstractExpression {
 public:
  FunctionExpression(const char* func_name,
                     const std::vector<AbstractExpression*>& children)
      : AbstractExpression(ExpressionType::FUNCTION),
        func_name_(func_name),
        func_ptr_(nullptr), 
        is_udf_(false) {
    for (auto& child : children) {
      children_.push_back(std::unique_ptr<AbstractExpression>(child));
    }
  }

  FunctionExpression(type::Value (*func_ptr)(const std::vector<type::Value>&),
                     type::TypeId return_type,
                     const std::vector<type::TypeId>& arg_types,
                     const std::vector<AbstractExpression*>& children)
      : AbstractExpression(ExpressionType::FUNCTION, return_type),
        func_ptr_(func_ptr),
        is_udf_(false) {
    for (auto& child : children) {
      children_.push_back(std::unique_ptr<AbstractExpression>(child));
    }
    CheckChildrenTypes(arg_types, children_, func_name_);
  }

  // For a built-in function
  void SetFunctionExpressionParameters(
      type::Value (*func_ptr)(const std::vector<type::Value>&),
      type::TypeId val_type,
      const std::vector<type::TypeId>& arg_types, bool is_udf) {
    func_ptr_ = func_ptr;
    return_value_type_ = val_type;
    CheckChildrenTypes(arg_types, children_, func_name_);
  }

  void SetUDFType(bool is_udf) {
    is_udf_ = is_udf;
  }

  type::Value Evaluate(
      const AbstractTuple* tuple1, const AbstractTuple* tuple2,
      UNUSED_ATTRIBUTE executor::ExecutorContext* context) const override {
    // for now support only one child
    std::vector<type::Value> child_values;
    for (auto& child : children_) {
      child_values.push_back(child->Evaluate(tuple1, tuple2, context));
    }
    type::Value ret;

    if(is_udf_) {
      // Populate the necessary fields
      auto func_catalog = catalog::FunctionCatalog::GetInstance();
      const catalog::UDFFunctionData &func_data = 
        func_catalog->GetFunction(func_name_, context->GetTransaction());

      if(func_data.func_is_present_) {
        CheckChildrenTypes(func_data.argument_types_, children_, func_name_);

        /*func_data.func_string_
        func_data.return_type_
        func_data.argument_types_ 

        Use these variables @@Haoran
        */


        //Logic for UDF
        //@@Haoran This is where you can add in your code
        // All the class fields are populated with the data you need.

        if (ret.GetElementType() != func_data.return_type_) {
        throw Exception(
            EXCEPTION_TYPE_EXPRESSION,
            "function " + func_name_ + " returned an unexpected type.");
        }
      }
      else {
         throw Exception("function " + func_name_ + " not found.");
      }
    }
    else {
      PL_ASSERT(func_ptr_ != nullptr);
      ret = func_ptr_(child_values);

      // if this is false we should throw an exception
      // TODO: maybe checking this every time is not neccesary? but it prevents
      // crashing
      if (ret.GetElementType() != return_value_type_) {
        throw Exception(
            EXCEPTION_TYPE_EXPRESSION,
            "function " + func_name_ + " returned an unexpected type.");
      }
    }
    
    return ret;
  }

  AbstractExpression* Copy() const override { return new FunctionExpression(*this); }

  std::string func_name_;

  virtual void Accept(SqlNodeVisitor* v) { v->Visit(this); }

 protected:
  FunctionExpression(const FunctionExpression& other)
      : AbstractExpression(other),
        func_name_(other.func_name_),
        func_ptr_(other.func_ptr_) ,
        is_udf_(false){}

 private:
  type::Value (*func_ptr_)(const std::vector<type::Value>&) = nullptr;

  // For UDFs
  bool is_udf_ = false;

  // throws an exception if children return unexpected types
  static void CheckChildrenTypes(
      const std::vector<type::TypeId>& arg_types,
      const std::vector<std::unique_ptr<AbstractExpression>>& children,
      const std::string& func_name) {
    if (arg_types.size() != children.size()) {
      throw Exception(EXCEPTION_TYPE_EXPRESSION,
                      "Unexpected number of arguments to function: " +
                          func_name + ". Expected: " +
                          std::to_string(arg_types.size()) + " Actual: " +
                          std::to_string(children.size()));
    }
    // check that the types are correct
    for (size_t i = 0; i < arg_types.size(); i++) {
      if (children[i]->GetValueType() != arg_types[i]) {
        throw Exception(EXCEPTION_TYPE_EXPRESSION,
                        "Incorrect argument type to fucntion: " + func_name +
                            ". Argument " + std::to_string(i) +
                            " expected type " +
                            type::Type::GetInstance(arg_types[i])->ToString() +
                            " but found " +
                            type::Type::GetInstance(children[i]->GetValueType())
                                ->ToString() +
                            ".");
      }
    }
  }
};

}  // namespace expression
}  // namespace peloton
