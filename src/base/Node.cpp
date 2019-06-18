// 
//     MINOTAUR -- It's only 1/2 bull
// 
//     (C)opyright 2008 - 2017 The MINOTAUR Team.
// 

/**
 * \file Node.cpp
 * \brief Define class Node for storing information about nodes.
 * \author Ashutosh Mahajan, Argonne National Laboratory
 */

#include <cmath>
#include <iostream>
#include <omp.h>

#include "MinotaurConfig.h"
#include "Branch.h"
#include "Modification.h"
#include "Node.h"
#include "Relaxation.h"

using namespace Minotaur;
using namespace std;

Node::Node()
  : branch_(BranchPtr()),
    depth_(0),
    id_(0),
    lb_(-INFINITY),
    pMods_(0), 
    rMods_(0), 
    parent_(NodePtr()),
    status_(NodeNotProcessed),
    vioVal_(0),
    tbScore_(0)
{
}


Node::Node(NodePtr parentNode, BranchPtr branch)
  : branch_(branch),
    depth_(0),
    id_(0),
    pMods_(0), 
    rMods_(0), 
    parent_(parentNode),
    status_(NodeNotProcessed),
    vioVal_(0),
    tbScore_(0)
{
  lb_ = parentNode->getLb();
}


Node::~Node()
{
  pMods_.clear();
  rMods_.clear();
  children_.clear();
}


void Node::addChild(NodePtr childNode)
{
  children_.push_back(childNode);
}


void Node::applyPMods(ProblemPtr p)
{
  ModificationConstIterator mod_iter;
  ModificationPtr mod;
  // first apply the mods that created this node from its parent
  if (branch_) {
    for (mod_iter=branch_->pModsBegin(); mod_iter!=branch_->pModsEnd(); 
        ++mod_iter) {
      mod = *mod_iter;
      mod->applyToProblem(p);
    }
  }
  // now apply any other mods that were added while processing it.
  for (mod_iter=pMods_.begin(); mod_iter!=pMods_.end(); ++mod_iter) {
    mod = *mod_iter;
    mod->applyToProblem(p);
  }
}


void Node::applyRMods(RelaxationPtr rel)
{
  ModificationConstIterator mod_iter;
  ModificationPtr mod;
  // first apply the mods that created this node from its parent
  if (branch_) {
    for (mod_iter=branch_->rModsBegin(); mod_iter!=branch_->rModsEnd(); 
        ++mod_iter) {
      mod = *mod_iter;
      mod->applyToProblem(rel);
    }
  }
  // now apply any other mods that were added while processing it.
  for (mod_iter=rMods_.begin(); mod_iter!=rMods_.end(); ++mod_iter) {
    mod = *mod_iter;
    mod->applyToProblem(rel);
  }
}


void Node::applyRModsTrans(RelaxationPtr rel)
{
  ModificationConstIterator mod_iter;
  ModificationPtr mod;
  ModificationPtr pmod1, mod2;
  ProblemPtr p;   //not used, just passed

  // first apply the mods that created this node from its parent
  if (branch_) {
    for (mod_iter=branch_->rModsBegin(); mod_iter!=branch_->rModsEnd();
        ++mod_iter) {
      mod = *mod_iter;
      //convert modifications applicable for other relaxation to this one
//#pragma omp critical
      //{
      //std::cout << "Node " << id_ << " thread " << omp_get_thread_num() << " mod " << std::endl;
      //mod->write(std::cout);
      pmod1 = mod->fromRel(rel, p);
      mod2 = pmod1->toRel(p, rel);
      mod2->applyToProblem(rel);
      //std::cout << " mod2 " << std::endl;
      //mod2->write(std::cout);
      //}
    }
  }
  // now apply any other mods that were added while processing it.
  for (mod_iter=rMods_.begin(); mod_iter!=rMods_.end(); ++mod_iter) {
    mod = *mod_iter;
    pmod1 = mod->fromRel(rel, p);
    mod2 = pmod1->toRel(p, rel);
    mod2->applyToProblem(rel);
  }
}


void Node::applyMods(RelaxationPtr rel, ProblemPtr p)
{
  applyPMods(p);
  applyRMods(rel);
}


void Node::removeChild(NodePtrIterator childNodeIter)
{
  children_.erase(childNodeIter);
}


void Node::removeChildren()
{
  children_.clear();
}


void Node::removeParent()
{
  //parent_.reset();
  parent_ = 0;
}


void Node::setDepth(UInt depth)
{
  depth_ = depth;
}


void Node::setId(UInt id)
{
  id_ = id;
}


void Node::setLb(double value)
{
  lb_ = value;
}


void Node::setWarmStart (WarmStartPtr ws) 
{ 
  ws_ = ws; 
}


void Node::undoPMods(ProblemPtr p)
{
  ModificationRConstIterator mod_iter;
  ModificationPtr mod;

  // explicitely request the const_reverse_iterator for rend():
  // for bug in STL C++ standard
  // http://stackoverflow.com/questions/2135094/gcc-reverse-iterator-comparison-operators-missing
  ModificationRConstIterator rend = pMods_.rend();  


  // first undo the mods that were added while processing the node.
  for (mod_iter=pMods_.rbegin(); mod_iter!= rend; ++mod_iter) {
    mod = *mod_iter;
    mod->undoToProblem(p);
  } 

  // now undo the mods that were used to create this node from its parent.
  if (branch_) {
    for (mod_iter=branch_->pModsRBegin(); mod_iter!=branch_->pModsREnd(); 
        ++mod_iter) {
      mod = *mod_iter;
      mod->undoToProblem(p);
    }
  }
}


void Node::undoRMods(RelaxationPtr rel)
{
  ModificationRConstIterator mod_iter;
  ModificationPtr mod;

  // explicitely request the const_reverse_iterator for rend():
  // for bug in STL C++ standard
  // http://stackoverflow.com/questions/2135094/gcc-reverse-iterator-comparison-operators-missing
  ModificationRConstIterator rend = rMods_.rend();  


  // first undo the mods that were added while processing the node.
  for (mod_iter=rMods_.rbegin(); mod_iter!= rend; ++mod_iter) {
    mod = *mod_iter;
    mod->undoToProblem(rel);
  } 

  // now undo the mods that were used to create this node from its parent.
  if (branch_) {
    for (mod_iter=branch_->rModsRBegin(); mod_iter!=branch_->rModsREnd(); 
        ++mod_iter) {
      mod = *mod_iter;
      mod->undoToProblem(rel);
    }
  }
}


void Node::undoRModsTrans(RelaxationPtr rel)
{
  ModificationRConstIterator mod_iter;
  ModificationPtr mod;
  ProblemPtr p;

  // explicitely request the const_reverse_iterator for rend():
  // for bug in STL C++ standard
  // http://stackoverflow.com/questions/2135094/gcc-reverse-iterator-comparison-operators-missing
  ModificationRConstIterator rend = rMods_.rend();
  ModificationPtr pmod1, mod2;

  // first undo the mods that were added while processing the node.
  for (mod_iter=rMods_.rbegin(); mod_iter!= rend; ++mod_iter) {
    mod = *mod_iter;
    //converting modifications applicable for one relaxation to another
    pmod1 = mod->fromRel(rel, p);
    mod2 = pmod1->toRel(p, rel);
    mod2->undoToProblem(rel);
  }

  // now undo the mods that were used to create this node from its parent.
  if (branch_) {
    for (mod_iter=branch_->rModsRBegin(); mod_iter!=branch_->rModsREnd();
        ++mod_iter) {
      mod = *mod_iter;
      pmod1 = mod->fromRel(rel, p);
      mod2 = pmod1->toRel(p, rel);
      mod2->undoToProblem(rel);
    }
  }
}


void Node::undoMods(RelaxationPtr rel, ProblemPtr p)
{
  undoPMods(p);
  undoRMods(rel);
}


void Node::updateBrCands(UInt index) {
  brCands_.push_back(index);
}


void Node::updateLastStrBranched(UInt index, double value) {
  if (index >= lastStrBranched_.size()) {
    lastStrBranched_.push_back(value);
  } else {
    lastStrBranched_[index] = value;
  }
}


void Node::updatePCDown(UInt index, double value) {
  if (index >= pseudoDown_.size()) {
    pseudoDown_.push_back(value);
  } else {
    pseudoDown_[index] = value;
  }
}


void Node::updatePCUp(UInt index, double value) {
  if (index >= pseudoUp_.size()) {
    pseudoUp_.push_back(value);
  } else {
    pseudoUp_[index] = value;
  }
}


void Node::updateTimesDown(UInt index, double value) {
  if (index >= timesDown_.size()) {
    timesDown_.push_back(value);
  } else {
    timesDown_[index] = value;
  }
}


void Node::updateTimesUp(UInt index, double value) {
  if (index >= timesUp_.size()) {
    timesUp_.push_back(value);
  } else {
    timesUp_[index] = value;
  }
}


void Node::write(std::ostream &out) const
{
  out << "Node ID: " << id_ << " at depth: " << depth_;
  if (parent_) { 
    out << " has parent ID: " << parent_->getId()
        << " lb = " << lb_ << " tbScore_ = " << tbScore_ << std::endl;
  }
  out << std::endl;
}


// Local Variables: 
// mode: c++ 
// eval: (c-set-style "k&r") 
// eval: (c-set-offset 'innamespace 0) 
// eval: (setq c-basic-offset 2) 
// eval: (setq fill-column 78) 
// eval: (auto-fill-mode 1) 
// eval: (setq column-number-mode 1) 
// eval: (setq indent-tabs-mode nil) 
// End:
