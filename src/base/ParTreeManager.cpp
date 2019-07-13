// 
//     MINOTAUR -- It's only 1/2 bull
// 
//     (C)opyright 2008 - 2017 The MINOTAUR Team.
// 

/**
 * \file ParTreeManager.cpp
 * \brief Define class ParTreeManager for managing tree for parallel Branch-and-Bound.
 * \author Prashant Palkar, IIT Bombay
 */

#include <cmath>
#include <omp.h>
#include "MinotaurConfig.h"
#include "Branch.h"
#include "Environment.h"
#include "Node.h"
#include "NodeHeap.h"
#include "NodeStack.h"
#include "Operations.h"
#include "Option.h"
#include "Timer.h"
#include "ParTreeManager.h"
//#include <omp.h>
using namespace Minotaur;
    
    
ParTreeManager::ParTreeManager(EnvPtr env) 
: bestLowerBound_(-INFINITY),
  bestUpperBound_(INFINITY),
  cutOff_(INFINITY),
  doVbc_(false),
  etol_(1e-6),
  size_(0),
  timer_(0)
{
  std::string s = env->getOptions()->findString("tree_search")->getValue();
  if ("dfs"==s) {
    searchType_ = DepthFirst;
  } else if ("bfs"==s) {
    searchType_ = BestFirst;
  } else if ("BthenD"==s) {
    searchType_ = BestThenDive;
  } else {
     assert (!"search strategy must be defined!");
  }

  switch (searchType_) {
   case (DepthFirst):
     active_nodes_ = (NodeStackPtr) new NodeStack();
     break;
   case (BestFirst):
   case (BestThenDive):
     active_nodes_ = (NodeHeapPtr) new NodeHeap(NodeHeap::Value);
     break;
   default:
     assert (!"search strategy must be defined!");
  }

  aNode_ = NodePtr();
  cutOff_ = env->getOptions()->findDouble("obj_cut_off")->getValue();
  tbRule_ = env->getOptions()->findString("tb_rule")->getValue();
  s = env->getOptions()->findString("vbc_file")->getValue();
  if (s!="") {
    vbcFile_.open(s.c_str());
    if (!vbcFile_.is_open()) {
      std::cerr << "cannot open file " << s << " for writing tree information.";
    } else {
      doVbc_ = true;
      vbcFile_ << "#TYPE: COMPLETE TREE" << std::endl;
      vbcFile_ << "#TIME: SET" << std::endl;
      vbcFile_ << "#BOUNDS: NONE" << std::endl;
      vbcFile_ << "#INFORMATION: STANDARD" << std::endl;
      vbcFile_ << "#NODE_NUMBER: NONE" << std::endl;
      timer_ = env->getNewTimer();
      timer_->start();
    }
  }
} 


ParTreeManager::~ParTreeManager()
{
  clearAll();
  delete active_nodes_;
  if (doVbc_) {
    vbcFile_.close();
    delete timer_;
  }
}


bool ParTreeManager::anyActiveNodesLeft()
{
  return !active_nodes_->isEmpty();
}


NodePtr ParTreeManager::branch(Branches branches, NodePtr node, WarmStartPtr ws)
{
  BranchPtr branch_p;
  NodePtr new_cand = NodePtr(); // NULL
  NodePtr child;
  bool is_first = false;
  if (searchType_ == DepthFirst || searchType_ == BestThenDive) {
    is_first = true;
  }
//#pragma omp critical
  //std::cout << "in parTM branch: node " << node->getId() << " thread: " << omp_get_thread_num() << std::endl;

  for (BranchConstIterator br_iter=branches->begin(); br_iter!=branches->end();
      ++br_iter) {
    branch_p = *br_iter;
    //branch_p->write(std::cout);
    child = (NodePtr) new Node(node, branch_p);
    child->setLb(node->getLb());
    child->setDepth(node->getDepth()+1);
    node->addChild(child);
    if (is_first) {
      insertCandidate_(child, true);
      is_first = false;
      new_cand = child;
    } else {
      // We make a copy of the pointer to warm-start, not the full copy of the
      // warm-start.
      child->setWarmStart(ws);
      insertCandidate_(child);
    }
    //std::cout << "inserting candidate\n";
  }
  if (doVbc_) {
    vbcFile_ << toClockTime(timer_->query()) << " P " << node->getId()+1 << " "
             << VbcSolved << std::endl;
    if (new_cand) {
      vbcFile_ << toClockTime(timer_->query()) << " P "
               << new_cand->getId()+1 << " " << VbcSolving << std::endl;
    }
  }
  aNode_ = new_cand; // can be NULL
  return new_cand;
}


void ParTreeManager::clearAll()
{
  NodePtr n;
  NodePtrIterator node_i;

  if (aNode_) {
    removeNode_(aNode_);
  }
  while (false==active_nodes_->isEmpty()) {
    n = active_nodes_->top();
    removeNode_(n);
    active_nodes_->pop();
  }
}


UInt ParTreeManager::getActiveNodes() const
{
  return active_nodes_->getSize();
}


NodePtr ParTreeManager::getCandidate()
{
  NodePtr node = NodePtr(); // NULL
  //aNode_.reset();
  aNode_ = 0;
  while (active_nodes_->getSize() > 0) {
    node = active_nodes_->top();
    // std::cout << "tm: node lb = " << node->getLb() << std::endl;
    if (shouldPrune_(node)) {
      // std::cout << "tm: node pruned." << std::endl;
      removeActiveNode(node);
      pruneNode(node);
      //node.reset(); // NULL
      node = 0;
    } else {
      if (doVbc_) {
        vbcFile_ << toClockTime(timer_->query()) << " P " << node->getId()+1
                 << " " << VbcSolving << std::endl;
      }
      break;
    }
  } 
  return node; // can be NULL
  // do not pop the head until the candidate has been processed.
}


double ParTreeManager::getCutOff()
{
  return cutOff_;
}


double ParTreeManager::getPerGap()
{
  // for minimization problems, gap = (ub - lb)/(ub) * 100
  // so that if one has a ub, she can say that the solution can not be more
  // than gap% away from the current ub.
  double gap = 0.0;
  if (bestUpperBound_ >= INFINITY) {
    gap = INFINITY;
  } else if (fabs(bestLowerBound_) < etol_) {
    gap = 100.0;
  } else {
    gap = (bestUpperBound_ - bestLowerBound_)/(fabs(bestUpperBound_)+etol_) 
      * 100.0;
    if (gap<0.0) {
      gap = 0.0;
    }
  }
  return gap;
}

double ParTreeManager::getPerGapPar(double treeLb)
{
  // for minimization problems, gap = (ub - lb)/(ub) * 100
  // so that if one has a ub, she can say that the solution can not be more
  // than gap% away from the current ub.
  //assert(bestLowerBound_ >= treeLb - etol_);
  double gap = 0.0;
  if (bestUpperBound_ >= INFINITY) {
    gap = INFINITY;
  } else if (fabs(treeLb) < etol_) {
    gap = 100.0;
  } else {
    gap = (bestUpperBound_ - treeLb)/(fabs(bestUpperBound_)+etol_) 
      * 100.0;
    if (gap<0.0) {
      gap = 0.0;
    }
  }
  //std::cout << "in parTM: gap " << gap << " treeLb " << treeLb << " ub " << bestUpperBound_ << "\n";
  return gap;
}


double ParTreeManager::getLb()
{
  return bestLowerBound_;
}


UInt ParTreeManager::getSize() const
{
  return size_;
}


double ParTreeManager::getUb()
{
  return bestUpperBound_;
}


void ParTreeManager::insertCandidate_(NodePtr node, bool pop_now)
{
  assert(size_>0);

  // set node id and depth
  node->setId(size_);
  node->setDepth(node->getParent()->getDepth()+1);
  if (tbRule_ == "twoChild") {
      if (pop_now) {
        node->setTbScore(2*(node->getParent()->getTbScore()));
      } else {
        node->setTbScore(2*(node->getParent()->getTbScore())+1);
      }
  } else if (tbRule_ == "FIFO") {
      node->setTbScore(node->getId());
  } else {
    node->setTbScore(node->getParent()->getTbScore());
  }

  ++size_;

  // add node to the heap/stack of active nodes. If pop_now is true, the node
  // is processed right after creating it; we don't
  // want to keep it in active_nodes (e.g. while diving)
  if (!pop_now) {
    active_nodes_->push(node);
  } 
  if (doVbc_) {
    vbcFile_ << toClockTime(timer_->query()) << " N "
      << node->getParent()->getId()+1 << " " << node->getId()+1
      << " " << VbcActive << std::endl;
  }
}


void ParTreeManager::insertRoot(NodePtr node)
{
  assert(size_==0);
  assert(active_nodes_->getSize()==0);

  node->setId(0);
  node->setDepth(0);
  active_nodes_->push(node);
  ++size_;
  if (doVbc_) {
    // father node color
    vbcFile_ << toClockTime(timer_->query()) << " N 0 1 " << VbcSolving
             << std::endl; 
  }
}


void ParTreeManager::pruneNode(NodePtr node)
{
  // XXX: if required do something before deleting the node.
  removeNode_(node);
}


void ParTreeManager::removeActiveNode(NodePtr node)
{
  if (doVbc_) {
    if (node->getStatus()==NodeOptimal) {
      vbcFile_ << toClockTime(timer_->query()) << " P "
        << active_nodes_->top()->getId()+1 << " " << VbcFeas << std::endl;
    } else if (node->getStatus()!=NodeInfeasible && node->getStatus()!=NodeHitUb) {
      vbcFile_ << toClockTime(timer_->query()) << " P "
               << active_nodes_->top()->getId()+1 << " " << VbcSolved << std::endl;
    } 
  }

  active_nodes_->pop();
  // active_nodes_->write(std::cout);
  //std::cout << "size of active nodes = " << active_nodes_.size() << std::endl;
  // dont remove the head until the candidate has been processed.
}


void ParTreeManager::removeNode_(NodePtr node) 
{
  NodePtr parent = node->getParent();
  NodePtrIterator node_i;

  // std::cout << "removing node xx " << node->getId() << std::endl;

  if (node->getId()>0) {
    NodePtr cNode;
    // check if the parent of this node has this node as its child. this check
    // is only for debugging purposes. may be removed if confident that
    // parents and children are properly linked.
    for (node_i = parent->childrenBegin(); node_i != parent->childrenEnd(); 
        ++node_i) {
      cNode = *node_i;
      if (cNode == node) {
        break;
      }
    }

    if (cNode==node) {
      parent->removeChild(node_i);
      node->removeParent();
      if (doVbc_) {
        VbcColors c;
        if (node->getStatus()==NodeHitUb) {
          c = VbcSubOpt;
        } else if (node->getStatus()==NodeInfeasible) {
          c = VbcInf;
        } else if (node->getStatus()==NodeOptimal) {
          c = VbcFeas;
        } else {
          c = VbcSubInf;
        }
        vbcFile_ << toClockTime(timer_->query()) << " P " << node->getId()+1 << " " 
                 << c << std::endl;
      }
      if (parent->getNumChildren() < 1) {
        removeNode_(parent);
      }

    } else {
      assert (!"Current node is not in its parent's list of children!");
    }
  }
  // std::cout << "node " << node->getId() << " use count = " <<
  // node.use_count() << std::endl;
  //node.reset();
  node = 0;
}


void ParTreeManager::setCutOff(double value)
{
  cutOff_ = value;
}


void ParTreeManager::setUb(double value)
{
  bestUpperBound_ = value;
  if (value < cutOff_) {
    cutOff_ = value;
  }
}


bool ParTreeManager::shouldDive()
{
  if (searchType_ == DepthFirst || searchType_ == BestThenDive) {
    return true;
  } 
  return false;
}


bool ParTreeManager::shouldPrune_(NodePtr node)
{
  double lb = node->getLb();
  if (lb > cutOff_ - etol_ || 
      fabs(bestUpperBound_-lb)/(fabs(bestUpperBound_)+etol_)*100 < etol_) {
    node->setStatus(NodeHitUb);
    return true;
  }
  return false;
}


double ParTreeManager::updateLb()
{
  // this could be an expensive operation. Try to avoid it.
  bestLowerBound_ = active_nodes_->getBestLB();

  return bestLowerBound_;
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
