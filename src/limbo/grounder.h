// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2016-2017 Christoph Schwering
// Licensed under the MIT license. See LICENSE file in the project root.
//
// A Grounder determines how many standard names need to be substituted for
// variables in a proper+ knowledge base and in queries.
//
// The grounder incrementally builds up the setup whenever AddClause(),
// PrepareForQuery(), or GuaranteeConsistency() are called. In particular,
// the relevant standard names (including the additional names) are managed and
// the clauses are regrounded accordingly. The Grounder is designed for fast
// backtracking.
//
// PrepareForQuery() should not be called before GuaranteeConsistency().
// Otherwise their behaviour is undefined.
//
// Quantification requires the temporary use of additional standard names.
// Grounder uses a temporary NamePool where names can be returned for later
// re-use. This NamePool is public for it can also be used to handle free
// variables in the representation theorem.


#ifndef LIMBO_GROUNDER_H_
#define LIMBO_GROUNDER_H_

#include <cassert>

#include <algorithm>
#include <list>
#include <memory>
#include <queue>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <limbo/clause.h>
#include <limbo/formula.h>
#include <limbo/setup.h>

#include <limbo/internal/hash.h>
#include <limbo/internal/intmap.h>
#include <limbo/internal/ints.h>
#include <limbo/internal/iter.h>
#include <limbo/internal/maybe.h>

namespace limbo {

class Grounder {
 public:
  typedef internal::size_t size_t;

  template<Symbol (Symbol::Factory::*CreateSymbol)(Symbol::Sort)>
  class Pool {
   public:
    Pool(Symbol::Factory* sf, Term::Factory* tf) : sf_(sf), tf_(tf) {}
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) = default;
    Pool& operator=(Pool&&) = default;

    Term Create(Symbol::Sort sort) {
      if (terms_[sort].empty()) {
        return tf_->CreateTerm((sf_->*CreateSymbol)(sort));
      } else {
        Term t = terms_[sort].back();
        terms_[sort].pop_back();
        return t;
      }
    }

    void Return(Term t) { terms_[t.sort()].push_back(t); }

    Term Get(Symbol::Sort sort, size_t i) {
      Term::Vector& ts = terms_[sort];
      while (i < ts.size()) {
        ts.push_back(tf_->CreateTerm((sf_->*CreateSymbol)(sort)));
      }
      return ts[i];
    }

   private:
    Symbol::Factory* const sf_;
    Term::Factory* const tf_;
    internal::IntMap<Symbol::Sort, Term::Vector> terms_;
  };

  typedef Pool<&Symbol::Factory::CreateName> NamePool;
  typedef Pool<&Symbol::Factory::CreateVariable> VariablePool;

  typedef Formula::SortedTermSet SortedTermSet;

  template<typename T>
  struct Ungrounded {
    typedef T value_type;
    struct Hash { internal::hash32_t operator()(const Ungrounded<T>& u) const { return u.val.hash(); } };
    typedef std::vector<Ungrounded> Vector;
    typedef std::unordered_set<Ungrounded, Hash> Set;

    bool operator==(const Ungrounded& u) const { return val == u.val; }
    bool operator!=(const Ungrounded& u) const { return !(*this != u); }

    T val;
    SortedTermSet vars;

   private:
    friend class Grounder;

    explicit Ungrounded(const T& val) : val(val) {}
  };

  struct Ply {
    typedef std::list<Ply> List;

    Ply(const Ply&) = delete;
    Ply& operator=(const Ply&) = delete;
    Ply(Ply&&) = default;
    Ply& operator=(Ply&&) = default;

    struct {
      Ungrounded<Clause>::Vector ungrounded;
      std::unique_ptr<Setup> full_setup;
      Setup::ShallowCopy shallow_setup;
    } clauses;
    struct {
      bool filter = false;  // enabled after consistency guarantee
      Ungrounded<Term>::Set ungrounded;
      SortedTermSet terms;
    } relevant;
    struct {
      SortedTermSet mentioned;       // names mentioned in clause or prepared-for query (but are not plus-names)
      SortedTermSet plus_max;        // plus-names that may be used for multiple purposes
      SortedTermSet plus_new;        // plus-names that may not be used for multiple purposes
      SortedTermSet plus_mentioned;  // plus-names that later occurred in formulas (which lead to plus_new names)
    } names;
    struct {
      Ungrounded<Literal>::Set ungrounded;  // literals in prepared-for query
      std::unordered_map<Term, std::unordered_set<Term>> map;  // grounded lhs-rhs index for clauses, prepared-for query
    } lhs_rhs;
    bool do_not_add_if_inconsistent = false;  // enabled for fix-literals

   private:
    friend class Grounder;

    Ply() = default;
  };

  struct Plies {
    typedef Ply::List::const_reverse_iterator iterator;
    enum Policy { kAll, kSinceSetup, kNew, kOld };

    iterator begin() const {
      switch (policy) {
        case kAll:
        case kSinceSetup:
        case kNew:
          return owner->plies_.rbegin();
        case kOld:
          return std::next(owner->plies_.rbegin());
      }
      return owner->plies_.rbegin();
    }

    iterator end() const {
      switch (policy) {
        case kAll:
        case kOld:
          return owner->plies_.rend();
        case kSinceSetup: {
          const iterator e = owner->plies_.rend();
          for (auto it = begin(); it != e; ++it) {
            if (it->clauses.full_setup) {
              return ++it;
            }
          }
          return e;
        }
        case kNew:
          return std::next(begin());
      }
    }

   private:
    friend class Grounder;

    explicit Plies(const Grounder* owner, Policy policy = kAll) : owner(owner), policy(policy) {}

    const Grounder* const owner;
    const Plies::Policy policy;
  };

  struct LhsTerms {
    struct First { Term operator()(const std::pair<Term, std::unordered_set<Term>>& p) const { return p.first; } };
    typedef std::unordered_map<Term, std::unordered_set<Term>>::const_iterator pair_iterator;
    typedef internal::transform_iterator<pair_iterator, First> term_iterator;

    struct New {
      New() = default;
      New(Plies::iterator begin, Plies::iterator end) : begin(begin), end(end) {}
      bool operator()(Term t) const {
        for (auto it = begin; it != end; ++it) {
          auto& m = it->lhs_rhs.map;
          if (m.find(t) != m.end()) {
            return false;
          }
        }
        return true;
      }
     private:
      Plies::iterator begin;
      Plies::iterator end;
    };

    typedef internal::filter_iterator<term_iterator, New> unique_term_iterator;

    struct Begin {
      explicit Begin(const Plies* plies) : plies(plies) {}
      unique_term_iterator operator()(const Plies::iterator it) const {
        term_iterator b = term_iterator(it->lhs_rhs.map.begin(), First());
        term_iterator e = term_iterator(it->lhs_rhs.map.end(), First());
        return unique_term_iterator(b, e, New(plies->begin(), it));
      }
     private:
      const Plies* plies;
    };

    struct End {
      explicit End(const Plies* plies) : plies(plies) {}
      unique_term_iterator operator()(const Plies::iterator it) const {
        term_iterator e = term_iterator(it->lhs_rhs.map.end(), First());
        return unique_term_iterator(e, e, New(plies->begin(), it));
      }
     private:
      const Plies* plies;
    };

    typedef internal::flatten_iterator<Plies::iterator, unique_term_iterator, Begin, End> iterator;

    iterator begin() const { return iterator(plies.begin(), plies.end(), Begin(&plies), End(&plies)); }
    iterator end()   const { return iterator(plies.end(),   plies.end(), Begin(&plies), End(&plies)); }

   private:
    friend class Grounder;

    LhsTerms(const Grounder* owner, Plies::Policy policy) : plies(owner, policy) {}

    const Plies plies;
  };

  struct RhsNames {
    typedef std::unordered_set<Term>::const_iterator name_iterator;

    struct Begin {
      Begin(Term t, name_iterator end) : t(t), end(end) {}
      name_iterator operator()(const Ply& p) const {
        auto it = p.lhs_rhs.map.find(t);
        return it != p.lhs_rhs.map.end() ? it->second.begin() : end;
      }
     private:
      Term t;
      name_iterator end;
    };

    struct End {
      End(Term t, name_iterator end) : t(t), end(end) {}
      name_iterator operator()(const Ply& p) const {
        auto it = p.lhs_rhs.map.find(t);
        return it != p.lhs_rhs.map.end() ? it->second.end() : end;
      }
     private:
      Term t;
      name_iterator end;
    };

    typedef internal::flatten_iterator<Plies::iterator, name_iterator, Begin, End> flat_iterator;
    typedef internal::singleton_iterator<Term> plus_iterator;
    typedef internal::joined_iterator<flat_iterator, plus_iterator> iterator;

    ~RhsNames() { if (!n_it->null()) { owner->name_pool_.Return(*n_it); } }

    iterator begin() const {
      flat_iterator b = flat_iterator(plies.begin(), plies.end(), Begin(t, ts.end()), End(t, ts.end()));
      flat_iterator e = flat_iterator(plies.end(),   plies.end(), Begin(t, ts.end()), End(t, ts.end()));
      if (n_it->null()) {
        n_it = plus_iterator(owner->name_pool_.Create(t.sort()));
      }
      return iterator(b, e, n_it);
    }

    iterator end() const {
      flat_iterator b = flat_iterator(plies.end(), plies.end(), Begin(t, ts.end()), End(t, ts.end()));
      flat_iterator e = flat_iterator(plies.end(), plies.end(), Begin(t, ts.end()), End(t, ts.end()));
      return iterator(b, e, plus_iterator());
    }

   private:
    friend class Grounder;

    RhsNames(Grounder* owner, Term t, Plies::Policy policy)
        : owner(owner), plies(owner, policy), t(t), n_it(Term()) { assert(n_it->null()); }

    Grounder* const owner;
    const Plies plies;
    const Term t;
    mutable plus_iterator n_it;
    const std::unordered_set<Term> ts = {};
  };

  struct Names {
    typedef internal::joined_iterator<typename SortedTermSet::value_iterator> plus_iterator;
    typedef internal::joined_iterator<typename SortedTermSet::value_iterator, plus_iterator> name_iterator;

    struct Begin {
      explicit Begin(Symbol::Sort sort) : sort(sort) {}
      name_iterator operator()(const Ply& p) const {
        auto b = plus_iterator(p.names.plus_max.begin(sort), p.names.plus_max.end(sort), p.names.plus_new.begin(sort));
        return name_iterator(p.names.mentioned.begin(sort), p.names.mentioned.end(sort), b);
      }
     private:
      Symbol::Sort sort;
    };

    struct End {
      explicit End(Symbol::Sort sort) : sort(sort) {}
      name_iterator operator()(const Ply& p) const {
        auto e = plus_iterator(p.names.plus_max.end(sort), p.names.plus_max.end(sort), p.names.plus_new.begin(sort));
        return name_iterator(p.names.mentioned.end(sort), p.names.mentioned.end(sort), e);
      }
     private:
      Symbol::Sort sort;
    };

    typedef internal::flatten_iterator<Plies::iterator, name_iterator, Begin, End> iterator;

    iterator begin() const { return iterator(plies.begin(), plies.end(), Begin(sort), End(sort)); }
    iterator end()   const { return iterator(plies.end(),   plies.end(), Begin(sort), End(sort)); }

   private:
    friend class Grounder;

    Names(const Grounder* owner, Symbol::Sort sort, Plies::Policy policy) : sort(sort), plies(owner, policy) {}

    const Symbol::Sort sort;
    const Plies plies;
  };

  class Undo {
   public:
    Undo() = default;
    Undo(const Undo&) = delete;
    Undo& operator=(const Undo&) = delete;
    Undo(Undo&& u) : owner_(u.owner_) { u.owner_ = nullptr; }
    Undo& operator=(Undo&& u) { owner_ = u.owner_; u.owner_ = nullptr; return *this; }
    ~Undo() {
      if (owner_) {
        owner_->UndoLast();
      }
      owner_ = nullptr;
    }

   private:
    friend class Grounder;

    explicit Undo(Grounder* owner) : owner_(owner) {}

    Grounder* owner_ = nullptr;
  };

  Grounder(Symbol::Factory* sf, Term::Factory* tf) : tf_(tf), name_pool_(sf, tf), var_pool_(sf, tf) {}
  Grounder(const Grounder&) = delete;
  Grounder& operator=(const Grounder&) = delete;
  Grounder(Grounder&&) = default;
  Grounder& operator=(Grounder&&) = default;
  ~Grounder() {
    while (!plies_.empty()) {
      plies_.erase(std::prev(plies_.end()));
    }
  }

  NamePool& temp_name_pool() { return name_pool_; }

  const Setup& setup() const { return plies_.empty() ? dummy_setup_ : last_ply().clauses.shallow_setup.setup(); }

  // 1. AddClause(c):
  // New ply.
  // Add c to ungrounded_clauses.
  // Add new names in c to names.
  // Add variables to vars, generate plus-names.
  // Re-ground.
  //
  // 2. PrepareForQuery(phi):
  // New ply.
  // Add new names in phi to names.
  // Add variables to vars, generate plus-names.
  // Re-ground.
  // Add f(.)=n, f(.)/=n pairs from grounded phi to lhs_rhs.
  //
  // 3. AddUnit(t=n):
  // New ply.
  // Add t=n to ungrounded_clauses.
  // If t=n contains a plus-name, add these to names and generate new plus-names.
  // If n is new, add n to names.
  // If either of the two cases, re-ground.
  //
  // 3. AddUnits(U):
  // New ply.
  // Add U to ungrounded_clauses.
  // If U contains t=n for new n, add n to names and re-ground.
  // (Note: in this case, all literals in U are of the form t'=n.)
  //
  // Re-ground:
  // Ground ungrounded_clauses for names and vars from last ply.
  // Add f(.)=n, f(.)/=n pairs from newly grounded clauses to lhs_rhs.
  // [ Close unit sets from previous AddUnit(U) plies under isomorphism with the names and vars from the last ply. ]
  //
  // We add the plus-names for quantifiers in the query in advance and ground
  // everything with them as if they occurred in the query.
  // So to determine the split and fix names, lhs_rhs suffices.
  //
  // Splits: {t=n | t in terms, n in lhs_rhs[t] or single new one}
  // Fixes: {t=n | t in terms, n in lhs_rhs[t] or single new one} or
  //        for every t in terms, n in lhs_rhs[t]:
  //        {t*=n* | t* in terms, n in lhs_rhs[t] or x in lhs_rhs[t], t=n, t*=n* isomorphic}
  //
  // Isomorphic literals: the bijection for a literal f(n1,...,nK)=n0 should only modify n1,...,nK, but not n0 unless
  // it is contained in n1,...,nK. Otherwise we'd add f(n1,...,nK)=n0, f(n1,...,nK)=n0*, etc. which obviously is
  // inconsistent.

  Setup::Result AddClause(const Clause& c, Undo* undo = nullptr, bool do_not_add_if_inconsistent = false) {
    auto r = internal::singleton_range(c);
    return AddClauses(r.begin(), r.end(), undo, do_not_add_if_inconsistent);
  }

  template<typename InputIt>
  Setup::Result AddClauses(InputIt first,
                           InputIt last,
                           Undo* undo = nullptr,
                           const bool do_not_add_if_inconsistent = false) {
    // Add c to ungrounded_clauses.
    // Add new names in c to names.
    // Add variables to vars, generate plus-names.
    // Re-ground.
    Ply& p = new_ply();
    for (; first != last; ++first) {
      Ungrounded<Clause> uc(*first);
      uc.val.Traverse([this, &p, &uc](Term t) {
        if (t.variable()) {
          uc.vars.insert(t);
        }
        if (t.name()) {
          if (!IsOccurringName(t)) {
            if (IsPlusName(t)) {
              p.names.plus_mentioned.insert(t);
            } else {
              p.names.mentioned.insert(t);
            }
          }
        }
        return true;
      });
      p.clauses.ungrounded.push_back(uc);
      CreateMaxPlusNames(uc.vars, 1);
    }
    CreateNewPlusNames(p.names.plus_mentioned);
    p.do_not_add_if_inconsistent = do_not_add_if_inconsistent;
    const Setup::Result r = Reground();
    if (undo) {
      *undo = Undo(this);
    }
    return r;
  }

  void PrepareForQuery(const Term t, Undo* undo = nullptr) {
    const Term x = var_pool_.Create(t.sort());
    const Literal a = Literal::Eq(t, x);
    const Formula::Ref phi = Formula::Factory::Atomic(Clause{a});
    PrepareForQuery(*phi, undo);
    var_pool_.Return(x);
  }

  void PrepareForQuery(const Formula& phi, Undo* undo = nullptr) {
    // New ply.
    // Add new names in phi to names.
    // Add variables to vars, generate plus-names.
    // Re-ground.
    // Add f(.)=n, f(.)/=n pairs from grounded phi to lhs_rhs.
    Ply& p = new_ply();
    phi.Traverse([this, &p](const Literal a) {
      Ungrounded<Literal> ua(a.pos() ? a : a.flip());
      a.Traverse([this, &p, &ua](const Term t) {
        if (t.name()) {
          if (!IsOccurringName(t)) {
            if (IsPlusName(t)) {
              p.names.plus_mentioned.insert(t);
            } else {
              p.names.mentioned.insert(t);
            }
          }
        } else if (t.variable()) {
          ua.vars.insert(t);
        }
        return true;
      });
      if (ua.val.lhs().function() && IsNewUngroundedLhsRhs(ua, Plies::kSinceSetup)) {
        last_ply().lhs_rhs.ungrounded.insert(ua);
      }
      return true;
    });
    CreateNewPlusNames(p.names.plus_mentioned);
    CreateMaxPlusNames(phi.n_vars());  // XXX or CreateNewPlusNames()?
    Reground();
    if (undo) {
      *undo = Undo(this);
    }
  }

  void GuaranteeConsistency(const Formula& alpha, Undo* undo) {
    // Collect ungrounded terms from query.
    // Close under terms in current setup.
    Ply& p = new_ply();
    p.relevant.filter = true;
    alpha.Traverse([this, &p](const Term t) {
      if (t.function()) {
        Ungrounded<Term> ut(t);
        t.Traverse([&ut](const Term x) { if (x.variable()) { ut.vars.insert(x); } return true; });
        p.relevant.ungrounded.insert(ut);
      }
      return false;
    });
    for (const Ungrounded<Term>& u : p.relevant.ungrounded) {
      for (const Term g : groundings(&u.val, &u.vars)) {
        p.relevant.terms.insert(g);
      }
    }
    CloseRelevanceUnderClauses(p.clauses.shallow_setup.setup().clauses(), Plies::kNew);
    GroundNewSetup();
    if (undo) {
      *undo = Undo(this);
    }
  }

  void GuaranteeConsistency(Term t, Undo* undo) {
    // Add t to ungrounded terms from query.
    // Close under terms in current setup.
    assert(t.primitive());
    Ply& p = new_ply();
    p.relevant.filter = true;
    p.relevant.ungrounded.insert(Ungrounded<Term>(t));
    p.relevant.terms.insert(t);
    CloseRelevanceUnderClauses(p.clauses.shallow_setup.setup().clauses(), Plies::kNew);
    GroundNewSetup();
    if (undo) {
      *undo = Undo(this);
    }
  }

  void UndoLast() { pop_ply(); }

  void Consolidate() { MergePlies(true); }

  Literal Variablify(Literal a) {
    assert(a.ground());
    Term::Vector ns;
    a.lhs().Traverse([&ns](Term t) {
      if (t.name() && std::find(ns.begin(), ns.end(), t) != ns.end()) {
        ns.push_back(t);
      }
      return true;
    });
    return a.Substitute([this, ns](Term t) {
      const size_t i = std::find(ns.begin(), ns.end(), t) - ns.begin();
      return i != ns.size() ? internal::Just(var_pool_.Get(t.sort(), i)) : internal::Nothing;
    }, tf_);
  }

  LhsTerms lhs_terms(Plies::Policy p = Plies::kAll) const { return LhsTerms(this, p); }
  // The additional name must not be used after RhsName's death.
  RhsNames rhs_names(Term t, Plies::Policy p = Plies::kSinceSetup) { return RhsNames(this, t, p); }
  Names names(Symbol::Sort sort, Plies::Policy p = Plies::kAll) const { return Names(this, sort, p); }

 private:
  template<typename T>
  struct Groundings {
   public:
    struct Assignments {
     public:
      struct NotX {
        explicit NotX(Term x) : x(x) {}

        bool operator()(Term y) const { return x != y; }

       private:
        const Term x;
      };

      struct DomainCodomain {
        DomainCodomain(const Grounder* owner, Plies::Policy policy) : owner(owner), policy(policy) {}

        std::pair<Term, Names> operator()(const Term x) const {
          return std::make_pair(x, owner->names(x.sort(), policy));
        }

       private:
        const Grounder* const owner;
        const Plies::Policy policy;
      };

      typedef internal::filter_iterator<SortedTermSet::all_values_iterator, NotX> vars_iterator;
      typedef internal::transform_iterator<vars_iterator, DomainCodomain> var_names_iterator;
      typedef internal::mapping_iterator<Term, Names::iterator> iterator;

      Assignments(const Grounder* owner, const SortedTermSet* vars, Plies::Policy policy, Term x, Term n)
          : vars(vars), owner(owner), policy(policy), x(x), n(n) {}

      iterator begin() const {
        auto p = NotX(x);
        auto f = DomainCodomain(owner, policy);
        auto b = vars->begin();
        auto e = vars->end();
        return iterator(var_names_iterator(vars_iterator(b, e, p), f), var_names_iterator(vars_iterator(e, e, p), f));
      }
      iterator end() const { return iterator(); }

     private:
      const SortedTermSet* const vars;
      const Grounder* const owner;
      const Plies::Policy policy;
      const Term x;
      const Term n;
    };

    struct Ground {
      Ground(Term::Factory* tf, const T* obj, Term x, Term n) : tf_(tf), obj(obj), x(x), n(n) {}
      T operator()(const typename Assignments::iterator::value_type& assignment) const {
        auto substitution = [this, &assignment](Term y) { return x == y ? internal::Just(n) : assignment(y); };
        return obj->Substitute(substitution, tf_);
      }
     private:
      Term::Factory* const tf_;
      const T* const obj;
      const Term x;
      const Term n;
    };

    typedef internal::transform_iterator<typename Assignments::iterator, Ground> iterator;

    Groundings(const Grounder* owner, const T* obj, const SortedTermSet* vars, Term x, Term n, Plies::Policy p)
        : x(x), n(n), assignments(owner, vars, p, x, n), ground(owner->tf_, obj, x, n) {}

    iterator begin() const { return iterator(assignments.begin(), ground); }
    iterator end()   const { return iterator(assignments.end(), ground); }

   private:
    Term x;
    Term n;
    Assignments assignments;
    Ground ground;
  };

  template<typename T>
  Groundings<T> groundings(const T* o, const SortedTermSet* vars, Plies::Policy p = Plies::kAll) const {
    return groundings(o, vars, Term(), Term(), p);
  }
  template<typename T>
  Groundings<T> groundings(const T* o, const SortedTermSet* vars, Term x, Term n, Plies::Policy p = Plies::kAll) const {
    return Groundings<T>(this, o, vars, x, n, p);
  }

  Ply& new_ply() {
    if (plies_.empty()) {
      plies_.push_back(Ply());
      Ply& p = plies_.back();
      p.clauses.full_setup = std::unique_ptr<Setup>(new Setup());
      p.clauses.shallow_setup = p.clauses.full_setup->shallow_copy();
      return p;
    } else {
      Ply& last_p = last_ply();
      plies_.push_back(Ply());
      Ply& p = plies_.back();
      p.clauses.shallow_setup = last_p.clauses.shallow_setup.setup().shallow_copy();
      p.relevant.filter = last_p.relevant.filter;
      return p;
    }
  }

  Plies plies(Plies::Policy p = Plies::kAll) const { return Plies(this, p); }

  Ply& last_ply() { assert(!plies_.empty()); return plies_.back(); }
  const Ply& last_ply() const { assert(!plies_.empty()); return plies_.back(); }

  Setup& last_setup() { return last_ply().clauses.shallow_setup.setup(); }
  const Setup& last_setup() const { return last_ply().clauses.shallow_setup.setup(); }

  void pop_ply() {
    assert(!plies_.empty());
    Ply& p = last_ply();
    for (const Term n : p.names.plus_max) {
      name_pool_.Return(n);
    }
    for (const Term n : p.names.plus_new) {
      name_pool_.Return(n);
    }
    plies_.pop_back();
  }

  bool IsNewUngroundedLhsRhs(const Ungrounded<Literal>& ua, Plies::Policy p) const {
    assert(ua.val.lhs().function());
    for (const Ply& p : plies(p)) {
      if (p.lhs_rhs.ungrounded.find(ua) != p.lhs_rhs.ungrounded.end()) {
        return false;
      }
    }
    return true;
  }

  bool IsNewLhsRhs(Literal a, Plies::Policy p) const {
    assert(a.primitive());
    for (const Ply& p : plies(p)) {
      auto it = p.lhs_rhs.map.find(a.lhs());
      if (it != p.lhs_rhs.map.end() && it->second.find(a.rhs()) != it->second.end()) {
        return false;
      }
    }
    return true;
  }

  bool IsNewRelevantTerm(Term t, Plies::Policy p) const {
    assert(t.ground() && t.function());
    for (const Ply& p : plies(p)) {
      if (p.relevant.terms.contains(t)) {
        return false;
      }
    }
    return true;
  }

  bool IsRelevantClause(const Clause& c, Plies::Policy p) const {
    if (!last_ply().relevant.filter) {
      return true;
    }
    for (const Ply& p : plies(p)) {
      if (!p.relevant.terms.all_empty() &&
          c.any([&p](const Literal a) { return !a.lhs().name() && p.relevant.terms.contains(a.lhs()); })) {
        return true;
      }
    }
    return false;
  }

  size_t nMaxPlusNames(Symbol::Sort sort) const {
    size_t n_names = 0;
    for (const Ply& p : plies_) {
      n_names += p.names.plus_max.n_values(sort);
    }
    return n_names;
  }

  bool IsOccurringName(Term n) const {
    assert(n.name());
    for (const Ply& p : plies_) {
      if (p.names.mentioned.contains(n) || p.names.plus_mentioned.contains(n)) {
        return true;
      }
    }
    return false;
  }

  bool IsPlusName(Term n) const {
    assert(n.name());
    for (const Ply& p : plies_) {
      if (p.names.plus_max.contains(n) || p.names.plus_new.contains(n)) {
        return true;
      }
    }
    return false;
  }

  void CreateMaxPlusNames(const Formula::SortCount& sc) {
    Ply& p = last_ply();
    for (const Symbol::Sort sort : sc.keys()) {
      const size_t need_total = sc[sort];
      if (need_total > 0) {
        const size_t have_already = nMaxPlusNames(sort);
        for (size_t i = have_already; i < need_total; ++i) {
          p.names.plus_max.insert(name_pool_.Create(sort));
        }
      }
    }
  }

  void CreateMaxPlusNames(const SortedTermSet& vars, size_t plus) {
    Ply& p = last_ply();
    for (const Symbol::Sort sort : vars.keys()) {
      size_t need_total = vars.n_values(sort);
      if (need_total > 0) {
        need_total += plus;
        const size_t have_already = nMaxPlusNames(sort);
        for (size_t i = have_already; i < need_total; ++i) {
          p.names.plus_max.insert(name_pool_.Create(sort));
        }
      }
    }
  }

  void CreateNewPlusNames(const SortedTermSet& ts) {
    Ply& p = last_ply();
    for (const Symbol::Sort sort : ts.keys()) {
      size_t need_total = ts.n_values(sort);
      for (size_t i = 0; i < need_total; ++i) {
        p.names.plus_new.insert(name_pool_.Create(sort));
      }
    }
  }

  void UpdateLhsRhs(Literal a, Plies::Policy p) {
    assert(a.ground());
    if (a.lhs().function() && IsNewLhsRhs(a, p)) {
      const Term t = a.lhs();
      const Term n = a.rhs();
      assert(t.ground() && n.name());
      Ply& p = last_ply();
      auto it = p.lhs_rhs.map.find(t);
      if (it == p.lhs_rhs.map.end()) {
        it = p.lhs_rhs.map.insert(std::make_pair(t, std::unordered_set<Term>())).first;
      }
      it->second.insert(n);
    }
  }

  void UpdateLhsRhs(const Clause& c, Plies::Policy p) {
    for (const Literal a : c) {
      UpdateLhsRhs(a, p);
    }
  }

  void UpdateRelevantTerms(Term t, Plies::Policy p) {
    assert(t.ground());
    if (t.function() && IsNewRelevantTerm(t, p)) {
      last_ply().relevant.terms.insert(t);
    }
  }

  bool UpdateRelevantTerms(const Clause& c, Plies::Policy p) {
    assert(c.ground());
    assert(!c.valid());
    if (c.any([this, p](const Literal a) { return !IsNewRelevantTerm(a.lhs(), p); })) {
      c.all([this, p](const Literal a) {
        UpdateRelevantTerms(a.lhs(), p);
        return true;
      });
      return true;
    } else {
      return false;
    }
  }

  template<typename ClauseRange>
  void CloseRelevanceUnderClauses(ClauseRange r, Plies::Policy p) {
    std::unordered_set<size_t> clauses;
    for (size_t i : r) {
      clauses.insert(i);
    }
rescan:
    for (auto it = clauses.begin(); it != clauses.end(); ++it) {
      const Clause c = last_ply().clauses.shallow_setup.setup().clause(*it);
      bool relevant = UpdateRelevantTerms(c, p);
      if (relevant) {
        clauses.erase(it);
        goto rescan;
      }
    }
  }

  bool InconsistencyCheck(const Ply& p, const Clause& c) {
    return !p.do_not_add_if_inconsistent || !c.unit() || !last_setup().Subsumes(Clause{c[0].flip()});
  }

  template<typename UnaryFunction, typename UnaryPredicate>
  void ForEachGrounding(UnaryFunction range, UnaryPredicate pred, Setup::Result* add_result = nullptr) {
    typedef decltype(range(std::declval<Ply>()).begin()) iterator;
    typedef typename iterator::value_type::value_type value_type;
    for (const Ply& p : plies_) {
      for (const Ungrounded<value_type>& u : range(p)) {
        for (const value_type& g : groundings(&u.val, &u.vars)) {
          assert(g.ground());
          pred(g, p, add_result);
          if (add_result && *add_result == Setup::kInconsistent) {
            return;
          }
        }
      }
    }
  }

  template<typename UnaryFunction, typename UnaryPredicate>
  void ForEachNewGrounding(UnaryFunction range, UnaryPredicate pred, Setup::Result* add_result = nullptr) {
    typedef decltype(range(std::declval<Ply>()).begin()) iterator;
    typedef typename iterator::value_type::value_type value_type;
    for (const Ply& p : plies(Plies::kOld)) {
      for (const Ungrounded<value_type>& u : range(p)) {
        for (const Term x : u.vars) {
          for (const Term n : names(x.sort(), Plies::kNew)) {
            for (const value_type& g : groundings(&u.val, &u.vars, x, n)) {
              assert(g.ground());
              pred(g, p, add_result);
              if (add_result && *add_result == Setup::kInconsistent) {
                return;
              }
            }
          }
        }
      }
    }
    const Ply& p = last_ply();
    for (const Ungrounded<value_type>& u : range(p)) {
      for (const value_type& g : groundings(&u.val, &u.vars)) {
        pred(g, p, add_result);
        if (add_result && *add_result == Setup::kInconsistent) {
          return;
        }
      }
    }
  }

  static void update_result(Setup::Result* add_result, Setup::Result r) {
    if (add_result) {
      switch (r) {
        case Setup::kOk:
          assert(*add_result != Setup::kInconsistent);
          *add_result = r;
          break;
        case Setup::kSubsumed:
          assert(*add_result != Setup::kInconsistent);
          break;
        case Setup::kInconsistent:
          *add_result = r;
          break;
      }
    }
  }

  Setup::Result Reground(bool minimize = false) {
    // Ground old clauses for names from last ply.
    // Ground new clauses for all names.
    // Add f(.)=n, f(.)/=n pairs from newly grounded clauses to lhs_rhs.
    Setup::Result add_result = Setup::kSubsumed;
    Ply& p = last_ply();
    ForEachNewGrounding(
        [](const Ply& p) { return p.clauses.ungrounded; },
        [this](const Clause& c, const Ply& p, Setup::Result* add_result) {
          if (!c.valid() && InconsistencyCheck(p, c)) {
            const Setup::Result r = last_setup().AddClause(c);
            update_result(add_result, r);
          }
        },
        &add_result);
    if (add_result == Setup::kInconsistent) {
      return add_result;
    }
    if (p.relevant.filter) {
      ForEachNewGrounding(
          [](const Ply& p) { return p.relevant.ungrounded; },
          [this](const Term t, const Ply&, Setup::Result*) {
            UpdateRelevantTerms(t, Plies::kSinceSetup);
          });
      CloseRelevanceUnderClauses(p.clauses.shallow_setup.new_clauses(), Plies::kSinceSetup);
      std::vector<Clause> new_clauses;
      Setup& s = last_setup();
      for (size_t i : p.clauses.shallow_setup.new_clauses()) {
        new_clauses.push_back(s.clause(i));
      }
      p.clauses.shallow_setup.Kill();
      p.clauses.shallow_setup = s.shallow_copy();
      for (const Clause& c : new_clauses) {
        if (IsRelevantClause(c, Plies::kSinceSetup)) {
          const Setup::Result r = s.AddClause(c);
          update_result(&add_result, r);
          assert(r != Setup::kInconsistent);
        }
      }
    }
    if (p.clauses.full_setup) {
      p.clauses.full_setup->Minimize();
    } else if (minimize) {
      p.clauses.shallow_setup.Minimize();
    }
    for (size_t i : p.clauses.shallow_setup.new_clauses()) {
      UpdateLhsRhs(last_setup().clause(i), Plies::kSinceSetup);
    }
    ForEachNewGrounding(
        [](const Ply& p) { return p.lhs_rhs.ungrounded; },
        [this](const Literal a, const Ply&, Setup::Result*) {
          UpdateLhsRhs(a, Plies::kSinceSetup);
        });
    return add_result;
  }

  void GroundNewSetup(bool minimize = false) {
    // Ground all clauses for all names.
    Ply& p = last_ply();
    assert(p.relevant.filter);
    assert(p.clauses.ungrounded.empty());
    assert(p.names.mentioned.all_empty() && p.names.plus_new.all_empty() && p.names.plus_max.all_empty());
    const Setup& old_s = p.clauses.shallow_setup.setup();
    std::unique_ptr<Setup> new_s(new Setup());
    for (size_t i : old_s.clauses()) {
      const Clause c = old_s.clause(i);
      if (IsRelevantClause(c, Plies::kNew)) {
        UpdateLhsRhs(c, Plies::kNew);
        new_s->AddClause(c);
      }
    }
    if (minimize) {
      new_s->Minimize();
    }
    p.clauses.full_setup = std::move(new_s);
    p.clauses.shallow_setup = p.clauses.full_setup->shallow_copy();
  }

  void MergePlies(bool minimize) {
    assert(!plies_.empty());
    auto p = plies_.end();
    for (auto it = plies_.begin(); it != plies_.end(); ++it) {
      if (it->clauses.full_setup) {
        p = it;
      }
    }
    if (p == plies_.end()) {
      return;
    }
    bool after = false;
    for (auto it = plies_.begin(); it != plies_.end(); ++it) {
      assert(!it->do_not_add_if_inconsistent);
      if (it == p) {
        after = true;
        continue;
      }
      p->clauses.ungrounded.insert(p->clauses.ungrounded.end(),
                                   it->clauses.ungrounded.begin(), it->clauses.ungrounded.end());
      p->names.mentioned.insert(it->names.mentioned);
      p->names.plus_max.insert(it->names.plus_max);
      p->names.plus_new.insert(it->names.plus_new);
      p->names.plus_mentioned.insert(it->names.plus_mentioned);
      if (after) {
        assert(!it->clauses.full_setup);
        p->clauses.shallow_setup.Immortalize();
        p->relevant.ungrounded.insert(it->relevant.ungrounded.begin(), it->relevant.ungrounded.end());
        p->relevant.terms.insert(it->relevant.terms);
        p->lhs_rhs.ungrounded.insert(it->lhs_rhs.ungrounded.begin(), it->lhs_rhs.ungrounded.end());
        for (auto& lhs_rhs : it->lhs_rhs.map) {
          auto lhs = p->lhs_rhs.map.find(lhs_rhs.first);
          if (lhs == p->lhs_rhs.map.end()) {
            p->lhs_rhs.map.insert(lhs_rhs);
          } else {
            lhs->second.insert(lhs_rhs.second.begin(), lhs_rhs.second.end());
          }
        }
      }
    }
    if (minimize) {
      p->clauses.full_setup->Minimize();
      p->clauses.shallow_setup = p->clauses.full_setup->shallow_copy();
    }
    plies_.erase(plies_.begin(), p);
    plies_.erase(std::next(p), plies_.end());
    assert(plies_.size() == 1);
  }

  Term::Factory* const tf_;
  NamePool name_pool_;
  VariablePool var_pool_;
  Ply::List plies_;
  Setup dummy_setup_;
};

}  // namespace limbo

#endif  // LIMBO_GROUNDER_H_

