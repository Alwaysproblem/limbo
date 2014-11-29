// vim:filetype=cpp:textwidth=80:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014 schwering@kbsg.rwth-aachen.de

#include <algorithm>
#include <cassert>
#include <utility>
#include "./formula.h"
#include "./compar.h"

namespace esbl {

// {{{

class Formula::Cnf {
 public:
  struct Comparator;
  class Disj;

  Cnf();
  explicit Cnf(const Disj& d);
  Cnf(const Cnf&);
  Cnf& operator=(const Cnf&);

  Cnf Substitute(const Unifier& theta) const;
  Cnf And(const Cnf& c) const;
  Cnf Or(const Cnf& c) const;

  bool operator==(const Cnf& c) const;

  void Minimize();

  bool ground() const;

  bool EntailedBy(Setup* setup, split_level k) const;
  bool EntailedBy(Setups* setups, split_level k) const;

  void AddToSetup(Setup* setup) const;
  void AddToSetups(Setups* setups) const;

  void Print(std::ostream* os) const;

 private:
  struct Equality;
  class KLiteral;
  class BLiteral;

  // Using unique_ptr prevents incomplete type errors.
  std::unique_ptr<std::set<Disj, LessComparator<Disj>>> ds_;
};

struct Formula::Cnf::Equality : public std::pair<Term, Term> {
 public:
  typedef std::set<Equality> Set;
  using std::pair<Term, Term>::pair;

  bool equal() const { return first == second; }
  bool ground() const { return first.ground() && second.ground(); }
};

class Formula::Cnf::KLiteral {
 public:
  struct Comparator;
  typedef std::set<KLiteral, Comparator> Set;

  KLiteral() = default;
  KLiteral(split_level k, const TermSeq& z, bool sign, const Cnf& phi)
      : k_(k), z_(z), sign_(sign), phi_(phi) {}
  KLiteral(const KLiteral&) = default;
  KLiteral& operator=(const KLiteral&) = default;

  bool operator==(const KLiteral& l) const {
    return z_ == l.z_ && phi_ == l.phi_ && sign_ == l.sign_ && k_ == l.k_;
  }

  KLiteral Flip() const { return KLiteral(k_, z_, !sign_, phi_); }
  KLiteral Positive() const { return KLiteral(k_, z_, true, phi_); }
  KLiteral Negative() const { return KLiteral(k_, z_, false, phi_); }

  KLiteral Substitute(const Unifier& theta) const {
    return KLiteral(k_, z_.Substitute(theta), sign_, phi_.Substitute(theta));
  }

  split_level k() const { return k_; }
  const TermSeq& z() const { return z_; }
  bool sign() const { return sign_; }
  const Cnf& phi() const { return phi_; }

  bool ground() const { return z_.ground() && phi_.ground(); }

 private:
  split_level k_;
  TermSeq z_;
  bool sign_;
  Cnf phi_;
};

class Formula::Cnf::BLiteral {
 public:
  struct Comparator;
  typedef std::set<BLiteral, Comparator> Set;

  BLiteral() = default;
  BLiteral(split_level k, const TermSeq& z, bool sign, const Cnf& neg_phi,
           const Cnf& psi)
      : k_(k), z_(z), sign_(sign), neg_phi_(neg_phi), psi_(psi) {}
  BLiteral(const BLiteral&) = default;
  BLiteral& operator=(const BLiteral&) = default;

  bool operator==(const BLiteral& l) const {
    return z_ == l.z_ && neg_phi_ == l.neg_phi_ && psi_ == l.psi_ &&
        sign_ == l.sign_ && k_ == l.k_;
  }

  BLiteral Flip() const { return BLiteral(k_, z_, !sign_, neg_phi_, psi_); }
  BLiteral Positive() const { return BLiteral(k_, z_, true, neg_phi_, psi_); }
  BLiteral Negative() const { return BLiteral(k_, z_, false, neg_phi_, psi_); }

  BLiteral Substitute(const Unifier& theta) const {
    return BLiteral(k_, z_.Substitute(theta), sign_,
                    neg_phi_.Substitute(theta), psi_.Substitute(theta));
  }

  split_level k() const { return k_; }
  const TermSeq& z() const { return z_; }
  bool sign() const { return sign_; }
  const Cnf& neg_phi() const { return neg_phi_; }
  const Cnf& psi() const { return psi_; }

  bool ground() const {
    return z_.ground() && neg_phi_.ground() && psi_.ground();
  }

 private:
  split_level k_;
  TermSeq z_;
  bool sign_;
  Cnf neg_phi_;
  Cnf psi_;
};

struct Formula::Cnf::Comparator {
  typedef Cnf value_type;

  bool operator()(const Cnf& c, const Cnf& d) const;
};

struct Formula::Cnf::KLiteral::Comparator {
  typedef KLiteral value_type;

  bool operator()(const KLiteral& l1, const KLiteral& l2) const {
    return comp(l1.z_, l1.phi_, l1.sign_, l1.k_,
                l2.z_, l2.phi_, l2.sign_, l2.k_);
  }

 private:
  LexicographicComparator<LessComparator<TermSeq>,
                          Formula::Cnf::Comparator,
                          LessComparator<bool>,
                          LessComparator<split_level>> comp;
};

struct Formula::Cnf::BLiteral::Comparator {
  typedef BLiteral value_type;

  bool operator()(const BLiteral& l1, const BLiteral& l2) const {
    return comp(l1.z_, l1.neg_phi_, l1.psi_, l1.sign_, l1.k_,
                l2.z_, l2.neg_phi_, l2.psi_, l2.sign_, l2.k_);
  }

 private:
  LexicographicComparator<LessComparator<TermSeq>,
                          Formula::Cnf::Comparator,
                          Formula::Cnf::Comparator,
                          LessComparator<bool>,
                          LessComparator<split_level>> comp;
};

class Formula::Cnf::Disj {
 public:
  struct Comparator;
  typedef std::set<Disj, LessComparator<Disj>> Set;

  Disj() = default;
  Disj(const Disj&) = default;
  Disj& operator=(const Disj&) = default;

  static Disj Concat(const Disj& c1, const Disj& c2);
  static Maybe<Disj> Resolve(const Disj& d1, const Disj& d2);
  Disj Substitute(const Unifier& theta) const;

  bool Subsumes(const Disj& d) const;
  bool Tautologous() const;

  // Just forwards to Comparator::operator(). The only purpose is that this
  // allows Cnf to declare a set of Disj.
  bool operator<(const Formula::Cnf::Disj& d) const;

  bool operator==(const Disj& d) const;

  void AddEq(const Term& t1, const Term& t2) {
    eqs_.insert(std::make_pair(t1, t2));
  }

  void AddNeq(const Term& t1, const Term& t2) {
    neqs_.insert(std::make_pair(t1, t2));
  }

  void ClearEqs() { eqs_.clear(); }
  void ClearNeqs() { neqs_.clear(); }

  void AddLiteral(const Literal& l) {
    c_.insert(l);
  }

  void AddNested(split_level k, const TermSeq& z, bool sign, const Cnf& phi) {
    ks_.insert(KLiteral(k, z, sign, phi));
  }

  void AddNested(split_level k, const TermSeq& z, bool sign, const Cnf& neg_phi,
                 const Cnf& psi) {
    bs_.insert(BLiteral(k, z, sign, neg_phi, psi));
  }

  bool ground() const;

  bool EntailedBy(Setup* setup, split_level k) const;
  bool EntailedBy(Setups* setups, split_level k) const;

  void AddToSetup(Setup* setup) const;
  void AddToSetups(Setups* setups) const;

  void Print(std::ostream* os) const;

 private:
  Equality::Set eqs_;
  Equality::Set neqs_;
  SimpleClause c_;
  KLiteral::Set ks_;
  BLiteral::Set bs_;
};

struct Formula::Cnf::Disj::Comparator {
  typedef Disj value_type;

  bool operator()(const Disj& c, const Disj& d) const {
    const size_t n1 = c.eqs_.size() + c.neqs_.size() + c.c_.size() +
        c.ks_.size() + c.bs_.size();
    const size_t n2 = d.eqs_.size() + d.neqs_.size() + d.c_.size() +
        d.ks_.size() + d.bs_.size();
    return comp(n1, c.eqs_, c.neqs_, c.c_, c.ks_, c.bs_,
                n2, d.eqs_, d.neqs_, d.c_, d.ks_, d.bs_);
  }

 private:
  LexicographicComparator<LessComparator<size_t>,
                          LessComparator<Equality::Set>,
                          LessComparator<Equality::Set>,
                          SimpleClause::Comparator,
                          LexicographicContainerComparator<KLiteral::Set>,
                          LexicographicContainerComparator<BLiteral::Set>> comp;
};

bool Formula::Cnf::Comparator::operator()(const Cnf& c, const Cnf& d) const {
  LexicographicContainerComparator<Disj::Set> compar;
  return compar(*c.ds_, *d.ds_);
}

Formula::Cnf::Cnf() : ds_(new Disj::Set()) {}

Formula::Cnf::Cnf(const Formula::Cnf::Disj& d) : Cnf() {
  ds_->insert(d);
}

Formula::Cnf::Cnf(const Cnf& c) : Cnf() {
  *ds_ = *c.ds_;
}

Formula::Cnf& Formula::Cnf::operator=(const Formula::Cnf& c) {
  *ds_ = *c.ds_;
  return *this;
}

Formula::Cnf Formula::Cnf::Substitute(const Unifier& theta) const {
  Cnf c;
  for (const Disj& d : *ds_) {
    c.ds_->insert(d.Substitute(theta));
  }
  return c;
}

Formula::Cnf Formula::Cnf::And(const Cnf& c) const {
  Cnf r = *this;
  r.ds_->insert(c.ds_->begin(), c.ds_->end());
  assert(r.ds_->size() <= ds_->size() + c.ds_->size());
  return r;
}

Formula::Cnf Formula::Cnf::Or(const Cnf& c) const {
  Cnf r;
  for (const Disj& d1 : *ds_) {
    for (const Disj& d2 : *c.ds_) {
      r.ds_->insert(Cnf::Disj::Concat(d1, d2));
    }
  }
  assert(r.ds_->size() <= ds_->size() * c.ds_->size());
  return r;
}

bool Formula::Cnf::operator==(const Formula::Cnf& c) const {
  return *ds_ == *c.ds_;
}

void Formula::Cnf::Minimize() {
  Disj::Set new_ds;
  for (const Disj& d : *ds_) {
    assert(d.ground());
    if (!d.Tautologous()) {
      Disj dd = d;
      dd.ClearEqs();
      dd.ClearNeqs();
      new_ds.insert(new_ds.end(), dd);
    }
  }
  std::swap(*ds_, new_ds);
  do {
    new_ds.clear();
    for (auto it = ds_->begin(); it != ds_->end(); ++it) {
      // Disj::operator< orders by clause length first, so subsumed clauses
      // are greater than the subsuming.
      for (auto jt = std::next(it); jt != ds_->end(); ) {
        if (it->Subsumes(*jt)) {
          jt = ds_->erase(jt);
        } else {
          Maybe<Disj> d = Disj::Resolve(*it, *jt);
          if (d) {
            new_ds.insert(d.val);
          }
          ++jt;
        }
      }
    }
    ds_->insert(new_ds.begin(), new_ds.end());
  } while (!new_ds.empty());
}

bool Formula::Cnf::ground() const {
  return std::all_of(ds_->begin(), ds_->end(),
                     [](const Disj& d) { return d.ground(); });
}

void Formula::Cnf::AddToSetup(Setup* setup) const {
  for (const Disj& d : *ds_) {
    d.AddToSetup(setup);
  }
}

void Formula::Cnf::AddToSetups(Setups* setups) const {
  for (const Disj& d : *ds_) {
    d.AddToSetups(setups);
  }
}

bool Formula::Cnf::EntailedBy(Setup* s, split_level k) const {
  return std::all_of(ds_->begin(), ds_->end(),
                     [s, k](const Disj& d) { return d.EntailedBy(s, k); });
}

bool Formula::Cnf::EntailedBy(Setups* s, split_level k) const {
  return std::all_of(ds_->begin(), ds_->end(),
                     [s, k](const Disj& d) { return d.EntailedBy(s, k); });
}

void Formula::Cnf::Print(std::ostream* os) const {
  *os << '(';
  for (auto it = ds_->begin(); it != ds_->end(); ++it) {
    if (it != ds_->begin()) {
      *os << " ^ ";
    }
    it->Print(os);
  }
  *os << ')';
}

Formula::Cnf::Disj Formula::Cnf::Disj::Concat(const Disj& d1, const Disj& d2) {
  Disj d = d1;
  d.eqs_.insert(d2.eqs_.begin(), d2.eqs_.end());
  d.neqs_.insert(d2.neqs_.begin(), d2.neqs_.end());
  d.c_.insert(d2.c_.begin(), d2.c_.end());
  d.ks_.insert(d2.ks_.begin(), d2.ks_.end());
  d.bs_.insert(d2.bs_.begin(), d2.bs_.end());
  return d;
}

template<class T>
bool ResolveLiterals(T* lhs, const T& rhs) {
  for (const auto& l : rhs) {
    const auto it = lhs->find(l.Flip());
    if (it != lhs->end()) {
      lhs->erase(it);
      return true;
    }
  }
  return false;
}

Maybe<Formula::Cnf::Disj> Formula::Cnf::Disj::Resolve(const Disj& d1,
                                                      const Disj& d2) {
  assert(d1.eqs_.empty() && d1.neqs_.empty());
  assert(d2.eqs_.empty() && d2.neqs_.empty());
  assert(d1.ground());
  assert(d2.ground());
  if (d1.c_.size() + d1.ks_.size() + d1.bs_.size() >
      d2.c_.size() + d2.ks_.size() + d2.bs_.size()) {
    return Resolve(d2, d1);
  }
  Disj r = d2;
  if (ResolveLiterals(&r.c_, d1.c_) ||
      ResolveLiterals(&r.ks_, d1.ks_) ||
      ResolveLiterals(&r.bs_, d1.bs_)) {
    return Perhaps(!r.Tautologous(), r);
  }
  return Nothing;
}

Formula::Cnf::Disj Formula::Cnf::Disj::Substitute(const Unifier& theta) const {
  Disj d;
  for (const auto& p : eqs_) {
    d.eqs_.insert(std::make_pair(p.first.Substitute(theta),
                                 p.second.Substitute(theta)));
  }
  for (const auto& p : neqs_) {
    d.eqs_.insert(std::make_pair(p.first.Substitute(theta),
                                 p.second.Substitute(theta)));
  }
  d.c_ = c_.Substitute(theta);
  for (const auto& k : ks_) {
    d.ks_.insert(k.Substitute(theta));
  }
  for (const auto& b : bs_) {
    d.bs_.insert(b.Substitute(theta));
  }
  return d;
}

bool Formula::Cnf::Disj::operator==(const Formula::Cnf::Disj& d) const {
  return eqs_ == d.eqs_ && neqs_ == d.neqs_ && c_ == d.c_ && ks_ == d.ks_ &&
      bs_ == d.bs_;
}

template<class T>
bool TautologousLiterals(const T& ls) {
  for (auto it = ls.begin(); it != ls.end(); ) {
    assert(ls.key_comp()(it->Negative(), it->Positive()));
    const auto jt = std::next(it);
    assert(ls.find(it->Flip()) == ls.end() || ls.find(it->Flip()) == jt);
    if (jt != ls.end() && !it->sign() && *it == jt->Flip()) {
      return true;
    }
    it = jt;
  }
  return false;
}

bool Formula::Cnf::Disj::Subsumes(const Disj& d) const {
  assert(ground());
  assert(d.ground());
  return std::includes(d.eqs_.begin(), d.eqs_.end(),
                       eqs_.begin(), eqs_.end(), neqs_.key_comp()) &&
      std::includes(d.neqs_.begin(), d.neqs_.end(),
                    neqs_.begin(), neqs_.end(), neqs_.key_comp()) &&
      std::includes(d.c_.begin(), d.c_.end(),
                    c_.begin(), c_.end(), c_.key_comp()) &&
      std::includes(d.ks_.begin(), d.ks_.end(),
                    ks_.begin(), ks_.end(), ks_.key_comp()) &&
      std::includes(d.bs_.begin(), d.bs_.end(),
                    bs_.begin(), bs_.end(), bs_.key_comp());
}

bool Formula::Cnf::Disj::Tautologous() const {
  assert(ground());
  return std::any_of(eqs_.begin(), eqs_.end(),
                     [](const Equality& e) { return e.equal(); }) ||
      std::any_of(neqs_.begin(), neqs_.end(),
                  [](const Equality& e) { return !e.equal(); }) ||
      TautologousLiterals(c_) ||
      TautologousLiterals(ks_) ||
      TautologousLiterals(bs_);
}

bool Formula::Cnf::Disj::ground() const {
  return std::all_of(eqs_.begin(), eqs_.end(),
                     [](const Equality& e) { return e.ground(); }) &&
      std::all_of(neqs_.begin(), neqs_.end(),
                  [](const Equality& e) { return e.ground(); }) &&
      c_.ground() &&
      std::all_of(ks_.begin(), ks_.end(),
                  [](const KLiteral& l) { return l.ground(); }) &&
      std::all_of(bs_.begin(), bs_.end(),
                  [](const BLiteral& l) { return l.ground(); });
}

void Formula::Cnf::Disj::AddToSetup(Setup* setup) const {
  assert(eqs_.empty() && neqs_.empty());
  assert(ks_.empty() && bs_.empty());
  setup->AddClause(Clause(Ewff::TRUE, c_));
}

void Formula::Cnf::Disj::AddToSetups(Setups* setups) const {
  assert(eqs_.empty() && neqs_.empty());
  assert(ks_.empty() && bs_.empty());
  setups->AddClause(Clause(Ewff::TRUE, c_));
}

bool Formula::Cnf::Disj::EntailedBy(Setup* s, split_level k) const {
  assert(bs_.empty());
  if (Tautologous()) {
    return true;
  }
  if (s->Entails(c_, k)) {
    return true;
  }
  for (const KLiteral& l : ks_) {
    // TODO(chs) (1) The negation of d.c_ should be added to the setup.
    // TODO(chs) (2) Or representation theorem instead of (1)?
    // That way, at least the SSA of knowledge/belief should be come out
    // correctly.
    if (l.phi().EntailedBy(s, l.k())) {
      return true;
    }
  }
  return false;
}

bool Formula::Cnf::Disj::EntailedBy(Setups* s, split_level k) const {
  if (Tautologous()) {
    return true;
  }
  if (s->Entails(c_, k)) {
    return true;
  }
  for (const KLiteral& l : ks_) {
    if (l.phi().EntailedBy(s, l.k())) {
      return true;
    }
  }
  // TODO(chs) ~neg_phi => psi, c.f. TODOs for Knowledge class; API changes
  // needed to account for ~neg_phi and psi at once.
  assert(bs_.empty());
  return false;
}

void Formula::Cnf::Disj::Print(std::ostream* os) const {
  *os << '(';
  for (auto it = eqs_.begin(); it != eqs_.end(); ++it) {
    if (it != eqs_.begin()) {
      *os << " v ";
    }
    *os << it->first << " = " << it->second;
  }
  for (auto it = neqs_.begin(); it != neqs_.end(); ++it) {
    if (!eqs_.empty() || it != neqs_.begin()) {
      *os << " v ";
    }
    *os << it->first << " != " << it->second;
  }
  for (auto it = c_.begin(); it != c_.end(); ++it) {
    if (!eqs_.empty() || !neqs_.empty() || it != c_.begin()) {
      *os << " v ";
    }
    *os << *it;
  }
  for (auto it = ks_.begin(); it != ks_.end(); ++it) {
    if (!eqs_.empty() || !neqs_.empty() || !c_.empty() ||
        it != ks_.begin()) {
      *os << " v ";
    }
    const char* s = it->sign() ? "" : "~";
    *os << s << "K_" << it->k() << '(';
    it->phi().Print(os);
    *os << ')';
  }
  for (auto it = bs_.begin(); it != bs_.end(); ++it) {
    if (!eqs_.empty() || !neqs_.empty() || !c_.empty() ||
        !ks_.empty() || it != bs_.begin()) {
      *os << " v ";
    }
    const char* s = it->sign() ? "" : "~";
    *os << s << '[' << it->z() << ']' << "B_" << it->k() << "(~";
    it->neg_phi().Print(os);
    *os << " => ";
    it->psi().Print(os);
    *os << ')';
  }
  *os << ')';
}

// }}}

// {{{

struct Formula::Equal : public Formula {
  bool sign;
  Term t1;
  Term t2;

  Equal(const Term& t1, const Term& t2) : Equal(true, t1, t2) {}
  Equal(bool sign, const Term& t1, const Term& t2)
      : sign(sign), t1(t1), t2(t2) {}

  Ptr Copy() const override { return Ptr(new Equal(sign, t1, t2)); }

  void Negate() override { sign = !sign; }

  void PrependActions(const TermSeq&) override {}

  void SubstituteInPlace(const Unifier& theta) override {
    t1 = t1.Substitute(theta);
    t2 = t2.Substitute(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    if (t1.is_variable()) {
      (*vs)[t1.sort()].insert(Variable(t1));
    }
    if (t2.is_variable()) {
      (*vs)[t2.sort()].insert(Variable(t2));
    }
  }

  std::pair<Truth, Ptr> Simplify() const override {
    if ((t1.ground() && t2.ground()) || t1 == t2) {
      const Truth t = (t1 == t2) == sign ? TRIVIALLY_TRUE : TRIVIALLY_FALSE;
      return std::make_pair(t, Ptr());
    }
    return std::make_pair(NONTRIVIAL, Copy());
  }

  Cnf MakeCnf(StdName::SortedSet*) const override {
    Cnf::Disj d;
    if (sign) {
      d.AddEq(t1, t2);
    } else {
      d.AddNeq(t1, t2);
    }
    return Cnf(d);
  }

  Ptr Regress(Term::Factory*, const DynamicAxioms&) const override {
    return Copy();
  }

  void Print(std::ostream* os) const override {
    const char* s = sign ? "=" : "!=";
    *os << '(' << t1 << ' ' << s << ' ' << t2 << ')';
  }
};

struct Formula::Lit : public Formula {
  Literal l;

  explicit Lit(const Literal& l) : l(l) {}

  Ptr Copy() const override { return Ptr(new Lit(l)); }

  void Negate() override { l = l.Flip(); }

  void PrependActions(const TermSeq& z) override { l = l.PrependActions(z); }

  void SubstituteInPlace(const Unifier& theta) override {
    l = l.Substitute(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    l.CollectVariables(vs);
  }

  std::pair<Truth, Ptr> Simplify() const override {
    return std::make_pair(NONTRIVIAL, Copy());
  }

  Cnf MakeCnf(StdName::SortedSet*) const override {
    Cnf::Disj d;
    d.AddLiteral(l);
    return Cnf(d);
  }

  Ptr Regress(Term::Factory* tf, const DynamicAxioms& axioms) const override {
    Maybe<Ptr> phi = axioms.RegressOneStep(tf, static_cast<const Atom&>(l));
    if (!phi) {
      return Copy();
    }
    if (!l.sign()) {
      phi.val->Negate();
    }
    return phi.val->Regress(tf, axioms);
  }

  void Print(std::ostream* os) const override {
    *os << l;
  }
};

struct Formula::Junction : public Formula {
  enum Type { DISJUNCTION, CONJUNCTION };

  Type type;
  Ptr l;
  Ptr r;

  Junction(Type type, Ptr l, Ptr r)
      : type(type), l(std::move(l)), r(std::move(r)) {}

  Ptr Copy() const override {
    return Ptr(new Junction(type, l->Copy(), r->Copy()));
  }

  void Negate() override {
    type = type == DISJUNCTION ? CONJUNCTION : DISJUNCTION;
    l->Negate();
    r->Negate();
  }

  void PrependActions(const TermSeq& z) override {
    l->PrependActions(z);
    r->PrependActions(z);
  }

  void SubstituteInPlace(const Unifier& theta) override {
    l->SubstituteInPlace(theta);
    r->SubstituteInPlace(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    // We assume formulas to be rectified, so that's OK. Otherwise, if x
    // occurred freely in l but bound in r, we need to take care not to delete
    // it with the second call.
    l->CollectFreeVariables(vs);
    r->CollectFreeVariables(vs);
  }

  std::pair<Truth, Ptr> Simplify() const override {
    auto p1 = l->Simplify();
    auto p2 = r->Simplify();
    if (type == DISJUNCTION) {
      if (p1.first == TRIVIALLY_TRUE || p2.first == TRIVIALLY_TRUE) {
        return std::make_pair(TRIVIALLY_TRUE, Ptr());
      }
      if (p1.first == TRIVIALLY_FALSE) {
        return p2;
      }
      if (p2.first == TRIVIALLY_FALSE) {
        return p1;
      }
    }
    if (type == CONJUNCTION) {
      if (p1.first == TRIVIALLY_FALSE || p2.first == TRIVIALLY_FALSE) {
        return std::make_pair(TRIVIALLY_FALSE, Ptr());
      }
      if (p1.first == TRIVIALLY_TRUE) {
        return p2;
      }
      if (p2.first == TRIVIALLY_TRUE) {
        return p1;
      }
    }
    assert(p1.first == NONTRIVIAL && p2.first == NONTRIVIAL);
    Ptr psi = Ptr(new Junction(type, std::move(p1.second),
                               std::move(p2.second)));
    return std::make_pair(NONTRIVIAL, std::move(psi));
  }

  Cnf MakeCnf(StdName::SortedSet* hplus) const override {
    const Cnf cnf_l = l->MakeCnf(hplus);
    const Cnf cnf_r = r->MakeCnf(hplus);
    if (type == DISJUNCTION) {
      return cnf_l.Or(cnf_r);
    } else {
      return cnf_l.And(cnf_r);
    }
  }

  Ptr Regress(Term::Factory* tf, const DynamicAxioms& axioms) const override {
    Ptr ll = l->Regress(tf, axioms);
    Ptr rr = r->Regress(tf, axioms);
    return Ptr(new Junction(type, std::move(ll), std::move(rr)));
  }

  void Print(std::ostream* os) const override {
    const char c = type == DISJUNCTION ? 'v' : '^';
    *os << '(' << *l << ' ' << c << ' ' << *r << ')';
  }
};

struct Formula::Quantifier : public Formula {
  enum Type { EXISTENTIAL, UNIVERSAL };

  Type type;
  Variable x;
  Ptr phi;

  Quantifier(Type type, const Variable& x, Ptr phi)
      : type(type), x(x), phi(std::move(phi)) {}

  Ptr Copy() const override {
    return Ptr(new Quantifier(type, x, phi->Copy()));
  }

  void Negate() override {
    type = type == EXISTENTIAL ? UNIVERSAL : EXISTENTIAL;
    phi->Negate();
  }

  void PrependActions(const TermSeq& z) override {
    assert(std::find(z.begin(), z.end(), x) == z.end());
    phi->PrependActions(z);
  }

  void SubstituteInPlace(const Unifier& theta) override {
    x = Variable(x.Substitute(theta));
    phi->SubstituteInPlace(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    phi->CollectFreeVariables(vs);
    (*vs)[x.sort()].erase(x);
  }

  std::pair<Truth, Ptr> Simplify() const override {
    auto p = phi->Simplify();
    if (type == EXISTENTIAL && p.first == TRIVIALLY_TRUE) {
      return std::make_pair(TRIVIALLY_TRUE, Ptr());
    }
    if (type == UNIVERSAL && p.first == TRIVIALLY_FALSE) {
      return std::make_pair(TRIVIALLY_FALSE, Ptr());
    }
    assert(p.first == NONTRIVIAL);
    Ptr psi = Ptr(new Quantifier(type, x, std::move(p.second)));
    return std::make_pair(NONTRIVIAL, std::move(psi));
  }

  Cnf MakeCnf(StdName::SortedSet* hplus) const override {
    StdName::Set& new_ns = (*hplus)[x.sort()];
    for (Term::Id id = 0; ; ++id) {
      assert(id <= static_cast<int>(new_ns.size()));
      const StdName n = Term::Factory::CreatePlaceholderStdName(id, x.sort());
      const auto p = new_ns.insert(n);
      if (p.second) {
        break;
      }
    }
    // Memorize names for this x because the recursive call might add additional
    // names which must not be substituted for this x.
    const StdName::Set this_ns = new_ns;
    const Cnf c = phi->MakeCnf(hplus);
    bool init = false;
    Cnf r;
    for (const StdName& n : this_ns) {
      const Cnf d = c.Substitute({{x, n}});
      if (!init) {
        r = d;
        init = true;
      } else if (type == EXISTENTIAL) {
        r = r.Or(d);
      } else {
        r = r.And(d);
      }
    }
    return r;
  }

  Ptr Regress(Term::Factory* tf, const DynamicAxioms& axioms) const override {
    Ptr psi = phi->Regress(tf, axioms);
    const Variable y = tf->CreateVariable(x.sort());
    psi->SubstituteInPlace({{x, y}});
    return Ptr(new Quantifier(type, y, std::move(psi)));
  }

  void Print(std::ostream* os) const override {
    const char* s = type == EXISTENTIAL ? "E " : "";
    *os << '(' << s << x << ". " << *phi << ')';
  }
};

struct Formula::Knowledge : public Formula {
  split_level k;
  TermSeq z;
  bool sign;
  Ptr phi;

  Knowledge(split_level k, const TermSeq z, bool sign, Ptr phi)
      : k(k), z(z), sign(sign), phi(std::move(phi)) {}

  Ptr Copy() const override {
    return Ptr(new Knowledge(k, z, sign, phi->Copy()));
  }

  void Negate() override { sign = !sign; }

  void PrependActions(const TermSeq& prefix) override {
    z.insert(z.begin(), prefix.begin(), prefix.end());
  }

  void SubstituteInPlace(const Unifier& theta) override {
    phi->SubstituteInPlace(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    phi->CollectFreeVariables(vs);
  }

  std::pair<Truth, Ptr> Simplify() const override {
    auto p = phi->Simplify();
    if (sign && p.first == TRIVIALLY_TRUE) {
      return std::make_pair(TRIVIALLY_TRUE, Ptr());
    }
    if (!sign && p.first == TRIVIALLY_FALSE) {
      return std::make_pair(TRIVIALLY_FALSE, Ptr());
    }
    Ptr know = Ptr(new Knowledge(k, z, sign, std::move(p.second)));
    return std::make_pair(NONTRIVIAL, std::move(know));
  }

  Cnf MakeCnf(StdName::SortedSet* hplus) const override {
    Cnf::Disj d;
    d.AddNested(k, z, sign, phi->MakeCnf(hplus));
    return Cnf(d);
  }

  Ptr Regress(Term::Factory*, const DynamicAxioms&) const override {
    assert(false);
    // TODO(chs) implement
    return Copy();
  }

  void Print(std::ostream* os) const override {
    *os << "K_" << k << '(' << *phi << ')';
  }
};

struct Formula::Belief : public Formula {
  split_level k;
  TermSeq z;
  bool sign;
  Ptr neg_phi;
  Ptr psi;

  Belief(split_level k, const TermSeq& z, bool sign, Ptr neg_phi, Ptr psi)
      : k(k), z(z), sign(sign), neg_phi(std::move(neg_phi)),
        psi(std::move(psi)) {}

  Ptr Copy() const override {
    return Ptr(new Belief(k, z, sign, neg_phi->Copy(), psi->Copy()));
  }

  void Negate() override { sign = !sign; }

  void PrependActions(const TermSeq& prefix) override {
    z.insert(z.begin(), prefix.begin(), prefix.end());
  }

  void SubstituteInPlace(const Unifier& theta) override {
    neg_phi->SubstituteInPlace(theta);
    psi->SubstituteInPlace(theta);
  }

  void CollectFreeVariables(Variable::SortedSet* vs) const {
    neg_phi->CollectFreeVariables(vs);
    psi->CollectFreeVariables(vs);
  }

  std::pair<Truth, Ptr> Simplify() const override {
    auto p1 = neg_phi->Simplify();
    auto p2 = psi->Simplify();
    if (sign && p1.first == TRIVIALLY_FALSE) {
      return std::make_pair(TRIVIALLY_FALSE, Ptr());
    }
    if (!sign && p2.first == TRIVIALLY_TRUE) {
      return std::make_pair(TRIVIALLY_TRUE, Ptr());
    }
    Ptr b = Ptr(new Belief(k, z, sign, std::move(p1.second),
                           std::move(p2.second)));
    return std::make_pair(NONTRIVIAL, std::move(b));
  }

  Cnf MakeCnf(StdName::SortedSet* hplus) const override {
    Cnf::Disj d;
    d.AddNested(k, z, sign, neg_phi->MakeCnf(hplus), psi->MakeCnf(hplus));
    return Cnf(d);
  }

  Ptr Regress(Term::Factory*, const DynamicAxioms&) const override {
    assert(false);
    // TODO(chs) implement
    return Copy();
  }

  void Print(std::ostream* os) const override {
    *os << "K_" << k << '(' << '~' << *neg_phi << " => " << *psi << ')';
  }
};

Formula::Ptr Formula::Eq(const Term& t1, const Term& t2) {
  return Ptr(new Equal(t1, t2));
}

Formula::Ptr Formula::Neq(const Term& t1, const Term& t2) {
  Ptr eq(std::move(Eq(t1, t2)));
  eq->Negate();
  return std::move(eq);
}

Formula::Ptr Formula::Lit(const Literal& l) {
  return Ptr(new struct Lit(l));
}

Formula::Ptr Formula::Or(Ptr phi1, Ptr phi2) {
  return Ptr(new Junction(Junction::DISJUNCTION,
                          std::move(phi1),
                          std::move(phi2)));
}

Formula::Ptr Formula::And(Ptr phi1, Ptr phi2) {
  return Ptr(new Junction(Junction::CONJUNCTION,
                          std::move(phi1),
                          std::move(phi2)));
}

Formula::Ptr Formula::OnlyIf(Ptr phi1, Ptr phi2) {
  return Or(Neg(std::move(phi1)), std::move(phi2));
}

Formula::Ptr Formula::If(Ptr phi1, Ptr phi2) {
  return Or(Neg(std::move(phi2)), std::move(phi1));
}

Formula::Ptr Formula::Iff(Ptr phi1, Ptr phi2) {
  return And(If(std::move(phi1->Copy()), std::move(phi2->Copy())),
             OnlyIf(std::move(phi1), std::move(phi2)));
}

Formula::Ptr Formula::Neg(Ptr phi) {
  phi->Negate();
  return std::move(phi);
}

Formula::Ptr Formula::Act(const Term& t, Ptr phi) {
  return Act(TermSeq{t}, std::move(phi));
}

Formula::Ptr Formula::Act(const TermSeq& z, Ptr phi) {
  phi->PrependActions(z);
  return std::move(phi);
}

Formula::Ptr Formula::Exists(const Variable& x, Ptr phi) {
  return Ptr(new Quantifier(Quantifier::EXISTENTIAL, x, std::move(phi)));
}

Formula::Ptr Formula::Forall(const Variable& x, Ptr phi) {
  return Ptr(new Quantifier(Quantifier::UNIVERSAL, x, std::move(phi)));
}

Formula::Ptr Formula::Know(split_level k, Ptr phi) {
  return Ptr(new Knowledge(k, {}, false, std::move(phi)));
}

Formula::Ptr Formula::Believe(split_level k, Ptr neg_phi, Ptr psi) {
  return Ptr(new Belief(k, {}, false, std::move(neg_phi), std::move(psi)));
}

void Formula::AddToSetup(Term::Factory* tf, Setup* setup) const {
  StdName::SortedSet hplus = tf->sorted_names();
  std::pair<Truth, Ptr> p = Simplify();
  if (p.first == TRIVIALLY_TRUE) {
    return;
  }
  if (p.first == TRIVIALLY_FALSE) {
    setup->AddClause(Clause::EMPTY);
    return;
  }
  Ptr phi = std::move(p.second);
  assert(phi);
  Cnf cnf = phi->MakeCnf(&hplus);
  cnf.Minimize();
  cnf.AddToSetup(setup);
}

void Formula::AddToSetups(Term::Factory* tf, Setups* setups) const {
  StdName::SortedSet hplus = tf->sorted_names();
  std::pair<Truth, Ptr> p = Simplify();
  if (p.first == TRIVIALLY_TRUE) {
    return;
  }
  if (p.first == TRIVIALLY_FALSE) {
    setups->AddClause(Clause::EMPTY);
    return;
  }
  Ptr phi = std::move(p.second);
  assert(phi);
  Cnf cnf = phi->MakeCnf(&hplus);
  cnf.Minimize();
  cnf.AddToSetups(setups);
}

bool Formula::EntailedBy(Term::Factory* tf, Setup* setup, split_level k) const {
  StdName::SortedSet hplus = tf->sorted_names();
  std::pair<Truth, Ptr> p = Simplify();
  if (p.first == TRIVIALLY_TRUE) {
    return true;
  }
  if (p.first == TRIVIALLY_FALSE) {
    return setup->Inconsistent(k);
  }
  Ptr phi = std::move(p.second);
  assert(phi);
  Cnf cnf = phi->MakeCnf(&hplus);
  cnf.Minimize();
  return cnf.EntailedBy(setup, k);
}

bool Formula::EntailedBy(Term::Factory* tf, Setups* setups,
                         split_level k) const {
  StdName::SortedSet hplus = tf->sorted_names();
  std::pair<Truth, Ptr> p = Simplify();
  if (p.first == TRIVIALLY_TRUE) {
    return true;
  }
  if (p.first == TRIVIALLY_FALSE) {
    return setups->Inconsistent(k);
  }
  Ptr phi = std::move(p.second);
  assert(phi);
  Cnf cnf = phi->MakeCnf(&hplus);
  cnf.Minimize();
  return cnf.EntailedBy(setups, k);
}

// }}}

}  // namespace esbl

