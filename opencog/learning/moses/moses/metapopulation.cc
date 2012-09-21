/** metapopulation.cc ---
 *
 * Copyright (C) 2010 Novemente LLC
 * Copyright (C) 2012 Poulin Holdings
 *
 * Authors: Nil Geisweiller, Moshe Looks, Linas Vepstas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "metapopulation.h"

namespace opencog {
namespace moses {

using std::pair;
using std::make_pair;
using boost::logic::tribool;
using boost::logic::indeterminate;
using namespace combo;

// bool deme_expander::create_deme(bscored_combo_tree_set::const_iterator exemplar)
bool deme_expander::create_deme(const combo_tree& exemplar)
{
    using namespace reduct;

    OC_ASSERT(_rep == NULL);
    OC_ASSERT(_deme == NULL);

    _exemplar = exemplar;

    if (logger().isDebugEnabled())
        logger().debug() << "Attempt to build rep from exemplar: " << _exemplar;

    // [HIGHLY EXPERIMENTAL]. It allows to select features
    // that provide the most information when combined with
    // the exemplar
    operator_set ignore_ops = _params.ignore_ops;
    if (_params.fstor) {
        // return the set of selected features as column index
        // (left most column corresponds to 0)
        auto selected_features = (*_params.fstor)(_exemplar);
        // add the complementary of the selected features (not
        // present in the exemplar) in ignore_ops
        auto exemplar_features = get_argument_abs_idx_from_zero_set(_exemplar);
        // arity_set exemplar_features = arity_set();
        unsigned arity = _params.fstor->ctable.get_arity();
        for (unsigned i = 0; i < arity; i++)
            if (selected_features.find(i) == selected_features.end()
                && exemplar_features.find(i) == exemplar_features.end())
                ignore_ops.insert(argument(i + 1));
        
        // debug print
        // std::vector<std::string> ios;
        // auto vertex_to_str = [](const vertex& v) {
        //     std::stringstream ss;
        //     ss << v;
        //     return ss.str();
        // };
        // boost::transform(ignore_ops, back_inserter(ios), vertex_to_str);
        // printlnContainer(ios);
        // ~debug print

        // Reformat the data so that dedundant inputs are grouped
        // (the number of redundant can possibly change from a
        // deme to another given that ignored input features can
        // change as well) as to possibly speed-up evaluation (if
        // the scorer implement ignore_idxs).
        std::set<arity_t> idxs;
        foreach(const vertex& v, ignore_ops)
            if (is_argument(v))
                idxs.insert(get_argument(v).abs_idx_from_zero());
        _cscorer.ignore_idxs(idxs);
    }

    // Build a representation by adding knobs to the exemplar,
    // creating a field set, and a mapping from field set to knobs.
    _rep = new representation(simplify_candidate,
                              simplify_knob_building,
                              _exemplar, _type_sig,
                              ignore_ops,
                              _params.perceptions,
                              _params.actions);

    // If the representation is empty, try the next
    // best-scoring exemplar.
    if (_rep->fields().empty()) {
        delete(_rep);
        _rep = NULL;
        logger().warn("The representation is empty, perhaps the reduct "
                      "effort for knob building is too high.");
    }

    if (!_rep) return false;

    // Create an empty deme.
    _deme = new deme_t(_rep->fields());

    return true;
}

int deme_expander::optimize_deme(int max_evals)
{
    if (logger().isDebugEnabled()) {
        logger().debug()
           << "Optimize deme; max evaluations allowed: "
           << max_evals;
    }

    complexity_based_scorer cpx_scorer =
        complexity_based_scorer(_cscorer, *_rep, _params.reduce_all);
    return _optimize(*_deme, cpx_scorer, max_evals);
}

void deme_expander::free_deme()
{
    delete _deme;
    delete _rep;
    _deme = NULL;
    _rep = NULL;
}

#if 0
/**
 * The metapopulation will store the expressions (as scored trees)
 * that were encountered during the learning process.  Only the
 * highest-scoring trees are typically kept.
 *
 * The metapopulation is updated in iterations. In each iteration, one
 * of its elements is selected as an exemplar. The exemplar is then
 * decorated with knobs and optimized, to create a new deme.  Members
 * of the deme are then folded back into the metapopulation.
 *
 * NOTE:
 *   cscore_base = scoring function (output composite scores)
 *   bscore_base = behavioral scoring function (output behaviors)
 */
struct metapopulation : bscored_combo_tree_ptr_set
{
    typedef metapopulation self;
    typedef bscored_combo_tree_set super;
    typedef super::value_type value_type;
    typedef instance_set<composite_score> deme_t;
    typedef deme_t::iterator deme_it;
    typedef deme_t::const_iterator deme_cit;

    // The goal of using unordered_set here is to have O(1) access time
    // to see if a combo tree is in the set, or not.
    typedef boost::unordered_set<combo_tree,
                                 boost::hash<combo_tree> > combo_tree_hash_set;

    // Init the metapopulation with the following set of exemplars.
    void init(const std::vector<combo_tree>& exemplars,
              const reduct::rule& simplify_candidate,
              const cscore_base& cscorer)
    {
        metapop_candidates candidates;
        foreach (const combo_tree& base, exemplars) {
            combo_tree si_base(base);
            simplify_candidate(si_base);

            penalized_behavioral_score pbs(_bscorer(si_base));
            // XXX Compute the bscore a second time.   The first time
            // was immediately above.  We do it again, because the
            // caching scorer lacks the correct signature.
            // composite_score csc(_cscorer (pbs, tree_complexity(si_base)));
            composite_score csc(cscorer(si_base));

            candidates[si_base] = composite_behavioral_score(pbs, csc);
        }

        bscored_combo_tree_set mps(candidates.begin(), candidates.end());
        update_best_candidates(mps);
        merge_candidates(mps);
    }

    /**
     *  Constuctor for the class metapopulation
     *
     * @param bases   Exemplars used to initialize the metapopulation
     * @param tt      Type signature of expression to be learned.
     *                That is, the expression must have this signature,
     *                relating the argument variables, and the output type.
     * @param si_ca   Reduct rule for reducing candidate combo trees.
     * @param si_kb   Reduct rule for reducing trees decorated with
     *                knobs.
     * @param sc      Function for scoring combo trees.
     * @param bsc     Behavior scoring function
     * @param opt     Algorithm that find best knob settings for a given
     *                exemplar decorated with knobs.
     * @param pa      Control parameters for this class.
     */
    metapopulation(const std::vector<combo_tree>& bases,
                   const type_tree& type_signature,
                   const reduct::rule& si_ca,
                   const reduct::rule& si_kb,
                   const cscore_base& sc,
                   const bscore_base& bsc,
                   optimizer_base& opt,
                   const metapop_parameters& pa = metapop_parameters()) :
        _dex(type_signature, si_ca, si_kb, sc, opt, pa),
        _bscorer(bsc), params(pa),
        _merge_count(0),
        _best_cscore(worst_composite_score),
        _cached_ddp(pa.diversity_p_norm,
                    pa.diversity_pressure,
                    pa.diversity_exponent)
    {
        init(bases, si_ca, sc);
    }

    // Like above but using a single base, and a single reduction rule.
    /// @todo use C++11 redirection
    metapopulation(const combo_tree& base,
                   const type_tree& type_signature,
                   const reduct::rule& si,
                   const cscore_base& sc, const bscore_base& bsc,
                   optimizer_base& opt,
                   const metapop_parameters& pa = metapop_parameters()) :
        _dex(type_signature, si, si, sc, opt, pa),
        _bscorer(bsc), params(pa),
        _merge_count(0),
        _best_cscore(worst_composite_score),
        _cached_ddp(pa.diversity_p_norm,
                    pa.diversity_pressure,
                    pa.diversity_exponent)
    {
        std::vector<combo_tree> bases(1, base);
        init(bases, si, sc);
    }

    ~metapopulation() {}
        
    /**
     * Return the best composite score.
     */
    const composite_score& best_composite_score() const
    {
        return _best_cscore;
    }

    /**
     * Return the best score.
     */
    score_t best_score() const
    {
        return get_score(_best_cscore);
    }

    /**
     * Return the set of candidates with the highest composite
     * scores.  These will all have the the same "best_composite_score".
     */
    const metapop_candidates& best_candidates() const
    {
        return _best_candidates;
    }

    /**
     * Return the best combo tree (shortest best candidate).
     */
    const combo_tree& best_tree() const
    {
        return _best_candidates.begin()->first;
    }

    typedef score_t dp_t;  // diversity_penalty type
    
    /**
     * Distort a diversity penalty component between 2
     * candidates. (actually not used apart from a comment of
     * aggregated_dps)
     */
    dp_t distort_dp(dp_t dp) const {
        return pow(dp, params.diversity_exponent);
    }
    /**
     * The inverse function of distort_dp normalized by the vector
     * size. Basically aggregated_dps(1/N * sum_i distort_dp(x_i)) ==
     * generalized_mean(x) where N is the size of x.
     */
    dp_t aggregated_dps(dp_t ddp_sum, unsigned N) const {
        return pow(ddp_sum / N, 1.0 / params.diversity_exponent);
    }
        
    /**
     * Compute the diversity penalty for all models of the metapopulation.
     *
     * If the diversity penalty is enabled, then punish the scores of
     * those exemplars that are too similar to the previous ones.
     * This may not make any difference for the first dozen exemplars
     * choosen, but starts getting important once the metapopulation
     * gets large, and the search bogs down.
     * 
     * XXX The implementation here results in a lot of copying of
     * behavioral scores and combo trees, and thus could hurt
     * performance by quite a bit.  To avoid this, we'd need to change
     * the use of bscored_combo_tree_set in this class. This would be
     * a fairly big task, and it's currently not clear that its worth
     * the effort, as diversity_penalty is not yet showing promising
     * results...
     */
    void set_diversity()
    {        
        bscored_combo_tree_ptr_set pool; // new metapopulation
        
        // structure to remember a partially aggredated distorted
        // diversity penalties between the candidates and the ones in
        // the pool (to avoid recomputing them)
        typedef bscored_combo_tree_ptr_set_it psi;
        typedef std::pair<psi, dp_t> bsct_dp_pair;
        std::vector<bsct_dp_pair> tmp;
        for (psi bsct_it = begin(); bsct_it != end(); ++bsct_it)
            tmp.push_back(bsct_dp_pair(bsct_it, 0.0));

        // // debug
        // std::atomic<unsigned> dp_count(0); // count the number of
        //                                    // calls of _cached_ddp
        // unsigned lhits = _cached_ddp.hits.load(),
        //     lmisses = _cached_ddp.misses.load();
        // // ~debug
        
        auto update_diversity_penalty = [&](bsct_dp_pair& v) {
            
            if (!pool.empty()) { // only do something if the pool is
                                 // not empty (WARNING: this assumes
                                 // that all diversity penalties are
                                 // initially zero
                
                bscored_combo_tree& bsct = *v.first;
                OC_ASSERT(get_bscore(bsct).size(),
                          "Behavioral score is needed for diversity!");
                
                // compute diversity penalty between bs and the last
                // element of the pool
                const bscored_combo_tree& last = *pool.crbegin();
                dp_t last_dp = this->_cached_ddp(&bsct, &last);

                // // debug
                // ++dp_count;
                // // ~debug
                
                // add it to v.second and compute the aggregated
                // diversity penalty
                dp_t adp;
                if (params.diversity_exponent > 0.0) {
                    v.second += last_dp;
                    adp = this->aggregated_dps(v.second, pool.size());
                } else {
                    v.second = std::max(v.second, last_dp);
                    adp = v.second;
                }
                
                // update v.first                
                get_composite_score(bsct).set_diversity_penalty(adp);
            }
        };

        while (tmp.size()) {

            // // debug
            // logger().fine("Pool =");
            // foreach (bscored_combo_tree* ptr, pool) {
            //     stringstream ss;
            //     ostream_bscored_combo_tree(ss, *ptr, true, true, true);
            //     logger().fine(ss.str());
            // }
            // // ~debug
            
            // update all diversity penalties of tmp
            OMP_ALGO::for_each(tmp.begin(), tmp.end(), update_diversity_penalty);

            // take the max score, insert in the pool and remove from tmp
            bscored_combo_tree_greater bsct_gt;
            auto gt = [&](const bsct_dp_pair& l,
                          const bsct_dp_pair& r) {
                return bsct_gt(*l.first, *r.first);
            };
            // note although we do use min_element it returns the
            // candidate with the best penalized score because we use
            // a greater_than function instead of a less_than function
            auto mit = OMP_ALGO::min_element(tmp.begin(), tmp.end(), gt);
            pool.transfer(mit->first, *this);
            tmp.erase(mit);
        }

        // // debug
        // logger().debug() << "Number of dst evals = " << dp_count;
        // logger().debug() << "Is lock free? " << dp_count.is_lock_free();
        // logger().debug() << "Number of hits = "
        //                  << _cached_ddp.hits.load() - lhits;
        // logger().debug() << "Number of misses = "
        //                  << _cached_ddp.misses.load() - lmisses;
        // logger().debug() << "Total number of hits = " << _cached_ddp.hits;
        // logger().debug() << "Total number of misses = " << _cached_ddp.misses;
        // // ~debug

        // Replace the existing metapopulation with the new one.
        swap(pool);
    }

    void log_selected_exemplar(const_iterator exemplar_it) {
        if (exemplar_it == cend()) {
            logger().debug() << "No exemplar found";
        } else {
            unsigned pos = std::distance(cbegin(), exemplar_it) + 1;
            logger().debug() << "Selected the " << pos << "th exemplar: "
                             << get_tree(*exemplar_it);
            logger().debug() << "With composite score :"
                             << get_composite_score(*exemplar_it);
        }
    }

    /**
     * Select the exemplar from the population. An exemplar is choosen
     * from the pool of candidates using a Boltzmann distribution
     * exp (-score / temperature).  Thus, they choosen exemplar will
     * typically be high-scoring, but not necessarily the highest-scoring.
     * This allows a range of reasonably-competitive candidates to be
     * explored, and, in practice, prooves to be much more effective
     * than a greedy algo which only selects the highest-scoring candidate.
     *
     * Current experimental evidence shows that temperatures in the
     * range of 6-12 work best for most problems, both discrete
     * (e.g. 4-parity) and continuous.
     *
     * @return the iterator of the selected exemplar, if no such
     *         exemplar exists then return end()
     */
    const_iterator select_exemplar()
    {
        OC_ASSERT(!empty(), "Empty metapopulation in select_exemplar().");

        logger().debug("Select exemplar");

        // Shortcut for special case, as sometimes, the very first time
        // through, the score is invalid.
        if (size() == 1) {
            const_iterator selex = cbegin();
            const combo_tree& tr = get_tree(*selex);
            if (_visited_exemplars.find(tr) == _visited_exemplars.end())
                _visited_exemplars.insert(tr);
            else selex = cend();
            log_selected_exemplar(selex);
            return selex;
        }

        vector<score_t> probs;
        // Set flag to true, when a suitable exemplar is found.
        bool found_exemplar = false;
#define UNEVALUATED_SCORE -1.0e37
        score_t highest_score = UNEVALUATED_SCORE;

        // The exemplars are stored in order from best score to worst;
        // the iterator follows this order.
        for (const_iterator it = begin(); it != end(); ++it) {

            score_t sc = get_penalized_score(*it);

            // Skip any exemplars we've already used in the past.
            const combo_tree& tr = get_tree(*it);
            if (_visited_exemplars.find(tr) == _visited_exemplars.end()) {
                probs.push_back(sc);
                found_exemplar = true;
                if (highest_score < sc) highest_score = sc;
            } else // hack: if the tree is visited then put a positive
                   // score so we know it must be ignored
#define SKIP_OVER_ME (1.0e38)
                probs.push_back(SKIP_OVER_ME);
        }

        // Nothing found, we've already tried them all.
        if (!found_exemplar) {
            log_selected_exemplar(cend());
            return cend();
        }

        // Compute the probability normalization, needed for the
        // roullete choice of exemplars with equal scores, but
        // differing complexities. Empirical work on 4-parity suggests
        // that a temperature of 3 or 4 works best.
        score_t inv_temp = 100.0f / params.complexity_temperature;
        score_t sum = 0.0f;
        // Convert scores into (non-normalized) probabilities
        foreach (score_t& p, probs) {
            // In case p has the max complexity (already visited) then
            // the probability is set to null
            p = (p > (0.1*SKIP_OVER_ME) ? 0.0f : expf((p - highest_score) * inv_temp));
            sum += p;
        }

        OC_ASSERT(sum > 0.0f, "There is an internal bug, please fix it");

        size_t fwd = distance(probs.begin(), roulette_select(probs.begin(),
                                                             probs.end(),
                                                             sum, randGen()));
        // cout << "select_exemplar(): sum=" << sum << " fwd =" << fwd
        // << " size=" << probs.size() << " frac=" << fwd/((float)probs.size()) << endl;
        const_iterator selex = std::next(begin(), fwd);

        // Mark the exemplar so we won't look at it again.
        _visited_exemplars.insert(get_tree(*selex));

        log_selected_exemplar(selex);
        return selex;
    }

    /// Given the current complexity temp, return the range of scores that
    /// are likely to be selected by the select_exemplar routine. Due to
    /// exponential decay of scores in select_exemplar(), this is fairly
    /// narrow: viz: e^30 = 1e13 ... We could probably get by with
    /// e^14 = 1.2e6
    //
    score_t useful_score_range() const
    {
        return params.complexity_temperature * 30.0 / 100.0;
    }

    /// Merge candidates in to the metapopulation.
    ///
    /// If the include-dominated flag is not set, the set of candidates
    /// might be changed during merge, with the dominated candidates
    /// removed during the merge. XXX Really?  It looks like the code
    /// does this culling *before* this method is called ...
    ///
    /// Safe to call in a multi-threaded context.
    template<typename Candidates>
    void merge_candidates(Candidates& candidates)
    {
        if (logger().isDebugEnabled()) {
            logger().debug("Going to merge %u candidates with the metapopulation",
                           candidates.size());
            if (logger().isFineEnabled()) {
                stringstream ss;
                ss << "Candidates to merge with the metapopulation:" << std::endl;
                foreach(const auto& cnd, candidates)
                    ostream_bscored_combo_tree(ss, cnd, true, true);
                logger().fine(ss.str());
            }
        }

        // Serialize access
        std::lock_guard<std::mutex> lock(_merge_mutex);

        // Note that merge_nondominated() is very cpu-expensive and
        // complex...
        if (params.include_dominated) {
            logger().debug("Insert all candidates in the metapopulation");
            foreach(const auto& cnd, candidates)
                insert(new bscored_combo_tree(cnd));
        } else {
            OC_ASSERT(false, "NOT IMPLEMENTED!!!!");
            // TODO let that aside for now!
            logger().debug("Insert non-dominated candidates in the metapopulation");
            unsigned old_size = size();
            // merge_nondominated(candidates, params.jobs);
            logger().debug("Inserted %u non-dominated candidates in the metapopulation",
                           size() - old_size);
        }

        unsigned old_size = size();
        logger().debug("Resize the metapopulation (%u), removing worst candidates",
                       old_size);
        resize_metapop();
        logger().debug("Removed %u candidates from the metapopulation",
                       old_size - size());

        if (logger().isDebugEnabled()) {
            logger().debug("Metapopulation size is %u", size());
            if (logger().isFineEnabled()) {
                stringstream ss;
                ss << "Metapopulation:" << std::endl;
                logger().fine(ostream(ss, -1, true, true).str());
            }
        }
    }

    /**
     * merge deme -- convert instances to trees, and save them.
     *
     * 1) cull the poorest scoring instances.
     * 2) convert set of instances to trees
     * 3) merge trees into the metapopulation, possibly using domination
     *    as the merge criterion.
     *
     * Return true if further deme exploration should be halted.
     *
     * This is almost but not quite thread-safe.  The use of
     * _visited_exemplars is not yet protected. There may be other
     * things.
     */
    bool merge_deme(deme_t* __deme, representation* __rep, size_t evals)
    {
        OC_ASSERT(__rep);
        OC_ASSERT(__deme);

        // It seems that, when using univariate multi-threaded opt,
        // the number of evals is (much) greater than the deme size.
        // I suspect this is a bug? XXX This needs investigation and
        // fixing.  On the other hand, univariate is quasi-obsolete...
        // (Note from Nil : Univariate overwrites candidates of the
        // deme during optimization so that is why the number of evals
        // is greater than the deme size).
        size_t eval_during_this_deme = std::min(evals, __deme->size());
        
        logger().debug("Close deme; evaluations performed: %d",
                       eval_during_this_deme);

        // Add, as potential exemplars for future demes, all unique
        // trees in the final deme.
        metapop_candidates pot_candidates;

        logger().debug("Sort the deme");

        // Sort the deme according to composite_score (descending order)
        std::sort(__deme->begin(), __deme->end(),
                  std::greater<scored_instance<composite_score> >());

        // Trim the deme down to size.  The point here is that the next
        // stage, select_candidates below, is very cpu-intensive; we
        // should keep only those candidates that will survive in the
        // metapop.  But what are these? Well, select_exemplar() uses
        // an exponential choice function; instances below a cut-off
        // score have no chance at all of getting selected. So just
        // eliminate them now, instead of later.
        //
        // However, trimming too much is bad: it can happen that none
        // of the best-scoring instances lead to a solution. So keep
        // around a reasonable pool. Wild choice ot 250 seems reasonable.
        if (min_pool_size < __deme->size()) {
            score_t top_sc = get_penalized_score(__deme->begin()->second);
            score_t bot_sc = top_sc - useful_score_range();

            for (size_t i = __deme->size()-1; 0 < i; --i) {
                const composite_score &cscore = (*__deme)[i].second;
                score_t score = get_penalized_score(cscore);
                if (score < bot_sc) {
                    __deme->pop_back();
                }
            }

            eval_during_this_deme =
                std::min(eval_during_this_deme, __deme->size());
        }

        ///////////////////////////////////////////////////////////////
        // select the set of candidates to add in the metapopulation //
        ///////////////////////////////////////////////////////////////
        typedef boost::shared_mutex mutex;
        typedef boost::shared_lock<mutex> shared_lock;
        typedef boost::unique_lock<mutex> unique_lock;

        mutex pot_cnd_mutex; // mutex for pot_candidates

        // NB, this is an anonymous function. In particular, some
        // compilers require that members be explicitly referenced
        // with this-> as otherwise we get compile fails:
        // https://bugs.launchpad.net/bugs/933906
        auto select_candidates =
            [&, this](const scored_instance<composite_score>& inst)
        {
            const composite_score& inst_csc = inst.second;
            score_t inst_sc = get_score(inst_csc);

            // if it's really bad stops
            if (inst_sc <= worst_score || !isfinite(inst_sc))
                return;

            // Get the combo_tree associated to inst, cleaned and reduced.
            //
            // @todo: below, the candidate is reduced possibly for the
            // second time.  This second reduction could probably be
            // avoided with some clever cache or something. (or a flag?)
            combo_tree tr = __rep->get_candidate(inst, true);

            // Look for tr in the list of potential candidates.
            // Return true if not found.
            auto thread_safe_tr_not_found = [&]() {
                shared_lock lock(pot_cnd_mutex);
                return pot_candidates.find(tr) == pot_candidates.end();
            };

            // XXX To make merge_deme thread safe, this needs to be
            // locked too.  (to avoid collision with threads updating
            // _visited, e.g. the MPI case.
            bool not_already_visited = this->_visited_exemplars.find(tr)
                == this->_visited_exemplars.end();

            // update the set of potential exemplars
            if (not_already_visited && thread_safe_tr_not_found()) {
                penalized_behavioral_score pbs; // empty bscore till
                                                // it gets computed
                composite_behavioral_score cbsc(pbs, inst_csc);

                unique_lock lock(pot_cnd_mutex);
                pot_candidates[tr] = cbsc;
            }
        };

        // We use
        //
        // deme->begin() + min(eval_during_this_deme, params.max_candidates)
        //
        // instead of
        //
        // deme->begin() + params.max_candidates,
        //
        // because we might have resized the deme to something larger
        // than the actual number of instances we placed into it (XXX
        // really?  Does this ever happen? Yes it did in very special
        // situation when MOSES was used in a sliced manner).
        //
        // Also, before we used params.max_candidates during
        // select_candidates, this was nice because it would discount
        // redundant candidates, but it turns out this introduces some
        // non-determinism when run with multiple threads. However
        // using params.max_candidates to determine the end iterator
        // doesn't introduce any indeterminism.
        //
        // Note: this step can be very time consuming; it currently
        // takes anywhere from 25 to 500(!!) millisecs per instance (!!)
        // for me; my (reduced, simplified) instances have complexity
        // of about 100. This seems too long/slow.
        deme_cit deme_begin = __deme->begin();
        unsigned max_pot_cnd = eval_during_this_deme;
        if (params.max_candidates >= 0)
            max_pot_cnd = std::min(max_pot_cnd, (unsigned)params.max_candidates);

        logger().debug("Select candidates to merge (amongst %u)", max_pot_cnd);

        deme_cit deme_end = __deme->begin() + max_pot_cnd;
        OMP_ALGO::for_each(deme_begin, deme_end, select_candidates);

        logger().debug("Selected %u candidates to be merged",
                       pot_candidates.size());
            
        // Behavioural scores are needed only if domination-based
        // merging is asked for, or if the diversity penalty is in use.
        // Save CPU time by not computing them.
        if (params.keep_bscore
            || !params.include_dominated || params.diversity_pressure > 0.0) {
            logger().debug("Compute behavioral score of %d selected candidates",
                           pot_candidates.size());

            auto compute_bscore = [this](metapop_candidates::value_type& cand) {
                composite_score csc = get_composite_score(cand.second);
                penalized_behavioral_score pbs = this->_bscorer(cand.first);
                cand.second = composite_behavioral_score(pbs, csc);
            };
            OMP_ALGO::for_each(pot_candidates.begin(), pot_candidates.end(),
                               compute_bscore);
        }

        logger().debug("Select only candidates not already in the metapopulation");
        bscored_combo_tree_set candidates = get_new_candidates(pot_candidates);
        logger().debug("Selected %u candidates (%u were in the metapopulation)",
                       candidates.size(), pot_candidates.size()-candidates.size());

        if (!params.include_dominated) {

            logger().debug("Remove dominated candidates");
            if (logger().isFineEnabled()) {
                stringstream ss;
                ss << "Candidates with their bscores before"
                    " removing the dominated candidates" << std::endl;
                foreach(const auto& cnd, candidates)
                    ostream_bscored_combo_tree(ss, cnd, true, true, true);
                logger().fine(ss.str());
            }

            size_t old_size = candidates.size();
            remove_dominated(candidates, params.jobs);

            logger().debug("Removed %u dominated candidates out of %u",
                           old_size - candidates.size(), old_size);
            if (logger().isFineEnabled()) {
                stringstream ss;
                ss << "Candidates with their bscores after"
                    " removing the dominated candidates" << std::endl;
                foreach(const auto& cnd, candidates)
                    ostream_bscored_combo_tree(ss, cnd, true, true, true);
                logger().fine(ss.str());
            }
        }

        // update the record of the best-seen score & trees
        update_best_candidates(candidates);

        bool done = false;
        if (params.merge_callback)
            done = (*params.merge_callback)(candidates, params.callback_user_data);
        merge_candidates(candidates);

        // update diversity penalties
        if (params.diversity_pressure > 0.0) {
            logger().debug("Compute diversity penalties of the metapopulation");
            set_diversity();
            if (logger().isFineEnabled()) {
                stringstream ss;
                ss << "Metapopulation after setting diversity:" << std::endl;
                logger().fine(ostream(ss, -1, true, true).str());
            }
        }

        return done;
    }

    /**
     * Weed out excessively bad scores. The select_exemplar() routine
     * picks an exemplar out of the metapopulation, using an
     * exponential distribution of the score. Scores that are much
     * worse than the best scores are extremely unlikely to be
     * choosen, so discard these from the metapopulation.  Keeping the
     * metapop small brings huge benefits to the mem usage and runtime
     * performance.
     *
     * However, lets not get over-zelous; if the metapop is too small,
     * then we have the nasty situation where none of the best-scoring
     * individuals lead to a solution.  Fix the minimum metapop size
     * to, oh, say, 250.
     *
     * But if the population starts exploding, this is also bad, as it
     * chews up RAM with unlikely exemplars. Keep it in check by
     * applying more and more stringent bounds on the allowable
     * scores.  The current implementation of useful_score_range()
     * returns a value a bit on the large size, by a factor of 2 or
     * so, so its quite OK to cut back on this value.
     */
    void resize_metapop() {
        if (size() > min_pool_size) {

            // pointers to deallocate
            std::vector<bscored_combo_tree*> ptr_seq;
            
            score_t top_score = get_penalized_score(*begin()),
                range = useful_score_range(),
                worst_score = top_score - range;
            
            // Erase all the lowest scores.  The metapop is in quasi-sorted
            // order (since the deme was sorted before being appended), so
            // this bulk remove mostly works "correctly". It is also 25%
            // faster than above. I think this is because the erase() above
            // causes the std::set to try to sort the contents, and this
            // ends up costing a lot.  I think... not sure.
            iterator it = std::next(begin(), min_pool_size);
            while (it != end()) {
                score_t sc = get_penalized_score(*it);
                if (sc < worst_score) break;
                it++;
            }

            while (it != end()) {
                ptr_seq.push_back(&*it);
                it = erase(it);
            }

            // Is the population still too large?  Yes, it is, if it is more
            // than 50 times the size of the current number of generations.
            // Realisitically, we could never explore more than 2% of a pool
            // that size.  For 10Bytes per table row, 20K rows, generation=500
            // this will still eat up tens of GBytes of RAM, and so is a
            // relatively lenient cap.
            // popsize cap =  50*(x+250)*(1+2*exp(-x/500))
            //
            // XXX TODO fix the cap so its more sensitive to the size of
            // each exemplar, right!?
            // size_t nbelts = get_bscore(*begin()).size();
            // double cap = 1.0e6 / double(nbelts);
            _merge_count++;
            double cap = 50.0;
            cap *= _merge_count + 250.0;
            cap *= 1 + 2.0*exp(- double(_merge_count) / 500.0);
            size_t popsz_cap = cap;
            size_t popsz = size();
            while (popsz_cap < popsz)
            {
                // Leave the first 50 alone.
                static const int offset = 50;
                int which = offset + randGen().randint(popsz-offset);
                // using std is necessary to break the ambiguity between
                // boost::next and std::next. Weirdly enough this appears
                // only 32bit arch
                iterator it = std::next(begin(), which);
                ptr_seq.push_back(&*it);
                erase(it);
                popsz --;
            }

            // remove them from _cached_cnd
            boost::sort(ptr_seq);
            _cached_ddp.erase_ptr_seq(ptr_seq);
        } // end of if (size() > min_pool_size)
    }
    
    // Return the set of candidates not present in the metapopulation.
    // This makes merging faster because it decreases the number of
    // calls of dominates.
    bscored_combo_tree_set get_new_candidates(const metapop_candidates& mcs)
    {
        bscored_combo_tree_set res;
        foreach (const bscored_combo_tree& cnd, mcs) {
            const combo_tree& tr = get_tree(cnd);
            const_iterator fcnd =
                OMP_ALGO::find_if(begin(), end(), [&](const bscored_combo_tree& v) {
                        return tr == get_tree(v); });
            if (fcnd == end())
                res.insert(cnd);
        }
        return res;
    }

    typedef pair<bscored_combo_tree_set,
                 bscored_combo_tree_set> bscored_combo_tree_set_pair;

    typedef std::vector<const bscored_combo_tree*> bscored_combo_tree_ptr_vec;
    typedef bscored_combo_tree_ptr_vec::iterator bscored_combo_tree_ptr_vec_it;
    typedef bscored_combo_tree_ptr_vec::const_iterator bscored_combo_tree_ptr_vec_cit;
    typedef pair<bscored_combo_tree_ptr_vec,
                 bscored_combo_tree_ptr_vec> bscored_combo_tree_ptr_vec_pair;

    // reciprocal of random_access_view
    static bscored_combo_tree_set
    to_set(const bscored_combo_tree_ptr_vec& bcv) {
        bscored_combo_tree_set res;
        foreach(const bscored_combo_tree* cnd, bcv)
            res.insert(*cnd);
        return res;
    }

    void remove_dominated(bscored_combo_tree_set& bcs, unsigned jobs = 1)
    {
        // get the nondominated candidates
        bscored_combo_tree_ptr_vec bcv = random_access_view(bcs);
        bscored_combo_tree_ptr_vec res = get_nondominated_rec(bcv, jobs);
        // get the dominated by set difference
        boost::sort(bcv); boost::sort(res);
        bscored_combo_tree_ptr_vec dif = set_difference(bcv, res);
        // remove the dominated ones
        foreach(const bscored_combo_tree* cnd_ptr, dif)
            bcs.erase(*cnd_ptr);
    }

    static bscored_combo_tree_set
    get_nondominated_iter(const bscored_combo_tree_set& bcs)
    {
        typedef std::list<bscored_combo_tree> bscored_combo_tree_list;
        typedef bscored_combo_tree_list::iterator bscored_combo_tree_list_it;
        bscored_combo_tree_list mcl(bcs.begin(), bcs.end());
        // remove all dominated candidates from the list
        for(bscored_combo_tree_list_it it1 = mcl.begin(); it1 != mcl.end();) {
            bscored_combo_tree_list_it it2 = it1;
            ++it2;
            if(it2 != mcl.end())
                for(; it2 != mcl.end();) {
                    tribool dom = dominates(get_bscore(*it1), get_bscore(*it2));
                    if(dom)
                        it2 = mcl.erase(it2);
                    else if(!dom) {
                        it1 = mcl.erase(it1);
                        it2 = mcl.end();
                    } else
                        ++it2;
                    if(it2 == mcl.end())
                        ++it1;
                }
            else
                ++it1;
        }
        return bscored_combo_tree_set(mcl.begin(), mcl.end());
    }

    // split in 2 of equal size
    static bscored_combo_tree_ptr_vec_pair
    inline split(const bscored_combo_tree_ptr_vec& bcv)
    {
        bscored_combo_tree_ptr_vec_cit middle = bcv.begin() + bcv.size() / 2;
        return make_pair(bscored_combo_tree_ptr_vec(bcv.begin(), middle),
                         bscored_combo_tree_ptr_vec(middle, bcv.end()));
    }

    bscored_combo_tree_ptr_vec
    get_nondominated_rec(const bscored_combo_tree_ptr_vec& bcv,
                         unsigned jobs = 1)
    {
        ///////////////
        // base case //
        ///////////////
        if (bcv.size() < 2) {
            return bcv;
        }
        //////////////
        // rec case //
        //////////////
//  The names in enum std::launch have not yet been standardized.
#if defined(__GNUC__) && (__GNUC__ == 4) && (__GNUC_MINOR__ >= 5) && (__GNUC_MINOR__ < 7)
 #define LAUNCH_SYNC std::launch::sync
#else
 #define LAUNCH_SYNC std::launch::deferred
#endif
        bscored_combo_tree_ptr_vec_pair bcv_p = split(bcv);
        if (jobs > 1) { // multi-threaded
            auto s_jobs = split_jobs(jobs); // pair
            // recursive calls
            std::future<bscored_combo_tree_ptr_vec> task =
                std::async(jobs > 1 ? std::launch::async : LAUNCH_SYNC,
                           bind(&self::get_nondominated_rec, this,
                                bcv_p.first, s_jobs.first));
            bscored_combo_tree_ptr_vec bcv2_nd =
                get_nondominated_rec(bcv_p.second, s_jobs.second);
            bscored_combo_tree_ptr_vec_pair res_p =
                get_nondominated_disjoint_rec(task.get(), bcv2_nd, jobs);
            // union and return
            append(res_p.first, res_p.second);
            return res_p.first;
        } else { // single-threaded
            // recursive calls
            bscored_combo_tree_ptr_vec
                bcv1_nd = get_nondominated_rec(bcv_p.first),
                bcv2_nd = get_nondominated_rec(bcv_p.second);
            bscored_combo_tree_ptr_vec_pair
                res_p = get_nondominated_disjoint_rec(bcv1_nd, bcv2_nd);
            // union and return
            append(res_p.first, res_p.second);
            return res_p.first;
        }
    }

    // return a pair of sets of nondominated candidates between bcs1
    // and bcs2, assuming none contain dominated candidates. Contrary
    // to what the name of function says, the 2 sets do not need be
    // disjoint, however there are inded disjoint according to the way
    // they are used in the code. The first (resp. second) element of
    // the pair corresponds to the nondominated candidates of bcs1
    // (resp. bcs2)
    bscored_combo_tree_set_pair
    get_nondominated_disjoint(const bscored_combo_tree_set& bcs1,
                              const bscored_combo_tree_set& bcs2,
                              unsigned jobs = 1)
    {
        bscored_combo_tree_ptr_vec_pair res_p =
            get_nondominated_disjoint_rec(random_access_view(bcs1),
                                          random_access_view(bcs2),
                                          jobs);
        return make_pair(to_set(res_p.first), to_set(res_p.second));
    }

    bscored_combo_tree_ptr_vec_pair
    get_nondominated_disjoint_rec(const bscored_combo_tree_ptr_vec& bcv1,
                                  const bscored_combo_tree_ptr_vec& bcv2,
                                  unsigned jobs = 1)
    {
        ///////////////
        // base case //
        ///////////////
        if (bcv1.empty() || bcv2.empty())
            return make_pair(bcv1, bcv2);
        else if (bcv1.size() == 1) {
            bscored_combo_tree_ptr_vec bcv_res1, bcv_res2;
            bscored_combo_tree_ptr_vec_cit it1 = bcv1.begin(),
                it2 = bcv2.begin();
            bool it1_insert = true; // whether *it1 is to be inserted
                                    // in bcv_res1
            for (; it2 != bcv2.end(); ++it2) {
                tribool dom = dominates(get_pbscore(**it1).first, get_pbscore(**it2).first);
                if (!dom) {
                    it1_insert = false;
                    bcv_res2.insert(bcv_res2.end(), it2, bcv2.end());
                    break;
                } else if (indeterminate(dom))
                    bcv_res2.push_back(*it2);
            }
            if (it1_insert)
                bcv_res1.push_back(*it1);
            return make_pair(bcv_res1, bcv_res2);
        }
        //////////////
        // rec case //
        //////////////
        // split bcs1 in 2
        bscored_combo_tree_ptr_vec_pair bcv1_p = split(bcv1);
        if(jobs > 1) { // multi-threaded
            unsigned jobs1 = jobs / 2;
            unsigned jobs2 = std::max(1U, jobs - jobs1);
            std::future<bscored_combo_tree_ptr_vec_pair> task =
                std::async(std::launch::async,
                           bind(&self::get_nondominated_disjoint_rec, this,
                                bcv1_p.first, bcv2, jobs1));
            bscored_combo_tree_ptr_vec_pair bcv_m2 =
                get_nondominated_disjoint_rec(bcv1_p.second, bcv2, jobs2);
            bscored_combo_tree_ptr_vec_pair bcv_m1 = task.get();
            // merge results
            append(bcv_m1.first, bcv_m2.first);
            boost::sort(bcv_m1.second); boost::sort(bcv_m2.second);
            bscored_combo_tree_ptr_vec bcv_m2_inter =
                set_intersection(bcv_m1.second, bcv_m2.second);
            return make_pair(bcv_m1.first, bcv_m2_inter);
        } else { // single-threaded
            bscored_combo_tree_ptr_vec_pair
                bcv_m1 = get_nondominated_disjoint_rec(bcv1_p.first, bcv2),
                bcv_m2 = get_nondominated_disjoint_rec(bcv1_p.second,
                                                       bcv_m1.second);
            // merge results
            append(bcv_m1.first, bcv_m2.first);
            return make_pair(bcv_m1.first, bcv_m2.second);
        }
    }

    // merge nondominated candidate to the metapopulation assuming
    // that bcs contains no dominated candidates within itself
    void merge_nondominated(bscored_combo_tree_set& bcs, unsigned jobs = 1)
    {
        bscored_combo_tree_ptr_vec bcv_mp = random_access_view(*this),
            bcv = random_access_view(bcs);
        bscored_combo_tree_ptr_vec_pair bcv_p =
            get_nondominated_disjoint_rec(bcv, bcv_mp, jobs);
        // remove the nondominates ones from the metapopulation
        boost::sort(bcv_mp);
        boost::sort(bcv_p.second);
        bscored_combo_tree_ptr_vec diff_bcv_mp =
            set_difference(bcv_mp, bcv_p.second);

        foreach (const bscored_combo_tree* cnd, diff_bcv_mp)
            erase(*cnd);

#if XXX_THIS_WONT_COMPILE_CANT_FIGURE_OUT_WHY
        // add the non dominates ones from bsc
        foreach (const bscored_combo_tree* cnd, bcv_p.first)
            insert(*cnd);
#endif // XXX_THIS_WONT_COMPILE_CANT_FIGURE_OUT_WHY
    }

    // Iterative version of merge_nondominated
    void merge_nondominated_iter(bscored_combo_tree_set& bcs)
    {
        for (bscored_combo_tree_set_it it1 = bcs.begin(); it1 != bcs.end();) {
            bscored_combo_tree_ptr_set_it it2 = begin();
            if (it2 == end())
                break;
            for (; it2 != end();) {
                tribool dom = dominates(get_bscore(*it1), get_bscore(*it2));
                if (dom)
                    erase(it2++);
                else if (!dom) {
                    bcs.erase(it1++);
                    it2 = end();
                } else
                    ++it2;
                if (it2 == end())
                    ++it1;
            }
        }
        // insert the nondominated candidates from bcs
        insert (bcs.begin(), bcs.end());
    }

    /**
     * return true if x dominates y
     *        false if y dominates x
     *        indeterminate otherwise
     */
    static inline tribool dominates(const behavioral_score& x,
                                    const behavioral_score& y)
    {
        // everything dominates an empty vector
        if (x.empty()) {
            if (y.empty())
                return indeterminate;
            return false;
        } else if (y.empty()) {
            return true;
        }

        tribool res = indeterminate;
        for (behavioral_score::const_iterator xit = x.begin(), yit = y.begin();
             xit != x.end();++xit, ++yit)
        {
            if (*xit > *yit) {
                if (!res)
                    return indeterminate;
                else
                    res = true;
            } else if (*yit > *xit) {
                if (res)
                    return indeterminate;
                else
                    res = false;
            }
        }
        return res;
    }

    /// Update the record of the best score seen, and the associated tree.
    /// Safe to call in a multi-threaded context.
    void update_best_candidates(const bscored_combo_tree_set& candidates)
    {
        if (candidates.empty())
            return;

        // Make this routine thread-safe.
        // XXX this lock probably doesn't have to be the same one
        // that merge uses.  I think. 
        std::lock_guard<std::mutex> lock(_merge_mutex);

        // Candidates are kept in penalized score order, not in
        // absolute score order.  Thus, we need to search through
        // the first few to find the true best score.  Also, there
        // may be several candidates with the best score.
        score_t best_sc = get_score(_best_cscore);
        complexity_t best_cpx = get_complexity(_best_cscore);

        foreach(const bscored_combo_tree& it, candidates)
        {
            const composite_score& cit = get_composite_score(it);
            score_t sc = get_score(cit);
            complexity_t cpx = get_complexity(cit);
            if ((sc > best_sc) || ((sc == best_sc) && (cpx <= best_cpx)))
            {
                if ((sc > best_sc) || ((sc == best_sc) && (cpx < best_cpx)))
                {
                    _best_cscore = cit;
                    best_sc = get_score(_best_cscore);
                    best_cpx = get_complexity(_best_cscore);
                    _best_candidates.clear();
                    logger().debug() << "New best score: " << _best_cscore;
                }
                _best_candidates.insert(it);
            }
        }
    }

    // log the best candidates
    void log_best_candidates() const
    {
        if (!logger().isInfoEnabled())
            return;

        if (best_candidates().empty())
            logger().info("No new best candidates");
        else {
            logger().info()
               << "The following candidate(s) have the best score "
               << best_composite_score();
            foreach(const bscored_combo_tree& cand, best_candidates()) {
                logger().info() << "" << get_tree(cand);
            }
        }
    }

    /**
     * stream out the metapopulation in decreasing order of their
     * score along with their scores (optionally complexity and
     * bscore).  If n is negative, then stream them all out.  Note
     * that the default sort order for the metapop is a penalized
     * scores and the smallest complexities, so that the best-ranked
     * candidates are not necessarily those with the best raw score.
     */
    template<typename Out, typename In>
    Out& ostream(Out& out, In from, In to, long n = -1,
                 bool output_score = true,
                 bool output_penalty = false,
                 bool output_bscore = false,
                 bool output_only_bests = false,
                 bool output_python = false)
    {
        if (!output_only_bests) {
            for (; from != to && n != 0; ++from, n--) {
                ostream_bscored_combo_tree(out, *from, output_score,
                                           output_penalty, output_bscore,
                                           output_python);
            }
            return out;
        }

        // Else, search for the top score...
        score_t best_score = worst_score;

        for (In f = from; f != to; ++f) {
            const bscored_combo_tree& bt = *f;
            score_t sc = get_score(bt);
            if (best_score < sc) best_score = sc;
        }

        // And print only the top scorers.
        // The problem here is that the highest scorers are not
        // necessarily ranked highest, as the ranking is a linear combo
        // of both score and complexity.
        for (In f = from; f != to && n != 0; ++f, n--) {
            const bscored_combo_tree& bt = *f;
            if (best_score <= get_score(bt)) {
                ostream_bscored_combo_tree(out, bt, output_score,
                                           output_penalty, output_bscore,
                                           output_python);
            }
        }
        return out;
    }

    // Like above, but assumes that from = begin() and to = end().
    template<typename Out>
    Out& ostream(Out& out, long n = -1,
                 bool output_score = true,
                 bool output_penalty = false,
                 bool output_bscore = false,
                 bool output_only_bests = false,
                 bool output_python = false)
    {
        return ostream(out, begin(), end(),
                       n, output_score, output_penalty,
                       output_bscore, output_only_bests, output_python);
    }

    ///  hmmm, apparently it's ambiguous with the one below, strange
    // // Like above but assumes that from = c.begin() and to = c.end()
    // template<typename Out, typename C>
    // Out& ostream(Out& out, const C& c, long n = -1,
    //              bool output_score = true,
    //              bool output_complexity = false,
    //              bool output_bscore = false,
    //              bool output_dominated = false)
    // {
    //     return ostream(out, c.begin(), c.end(),
    //                    n, output_score, output_complexity,
    //                    output_bscore, output_dominated);
    // }

    // Like above, but using std::cout.
    void print(long n = -1,
               bool output_score = true,
               bool output_penalty = false,
               bool output_bscore = false,
               bool output_only_bests = false)
    {
        ostream(std::cout, n, output_score, output_penalty,
                output_bscore, output_only_bests);
    }

    deme_expander _dex;

protected:
    static const unsigned min_pool_size = 250;
    
    const bscore_base& _bscorer; // behavioral score
    metapop_parameters params;

    size_t _merge_count;

    // The best score ever found during search.
    composite_score _best_cscore;

    // Trees with composite score equal to _best_cscore.
    metapop_candidates _best_candidates;

    // contains the exemplars of demes that have been searched so far
    combo_tree_hash_set _visited_exemplars;

    // lock to enable thread-safe deme merging.
    std::mutex _merge_mutex;

    /**
     * Cache for distorted diversity penalty. Maps a
     * std::set<bscored_combo_tree*> (only 2 elements to represent an
     * unordered pair) to an distorted diversity penalty. We don't use
     * {lru,prr}_cache because
     *
     * 1) we don't need a limit on the cache.
     *
     * 2) however we need a to remove the pairs containing deleted pointers
     */
    struct cached_ddp {
        // ctor
        cached_ddp(dp_t lp_norm, dp_t diversity_pressure, dp_t diversity_exponent)
            : p(lp_norm), dpre(diversity_pressure), dexp(diversity_exponent),
              misses(0), hits(0) {}

        // We use a std::set instead of a std::pair, little
        // optimization to deal with the symmetry of the distance
        typedef std::set<const bscored_combo_tree*> ptr_pair; 
        dp_t operator()(const bscored_combo_tree* cl, const bscored_combo_tree* cr)
        {
            ptr_pair cts = {cl, cr};
            // hit
            {
                shared_lock lock(mutex);
                auto it = cache.find(cts);
                if (it != cache.end()) {
                    ++hits;
                    return it->second;
                }
            }
            // miss
            dp_t dst = lp_distance(get_bscore(*cl), get_bscore(*cr), p),
                dp = dpre / (1.0 + dst),
                ddp = dexp > 0.0 ? pow(dp, dexp) : dp /* no distortion */;

            // // debug
            // logger().fine("dst = %f, dp = %f, ddp = %f", dst, dp, ddp);
            // // ~debug
            
            ++misses;
            {
                unique_lock lock(mutex);
                return cache[cts] = ddp;
            }
        }

        /**
         * Remove all keys containing any element of ptr_seq
         */
        void erase_ptr_seq(std::vector<bscored_combo_tree*> ptr_seq) {
            for (Cache::iterator it = cache.begin(); it != cache.end();) {
                if (!is_disjoint(ptr_seq, it->first))
                    it = cache.erase(it);
                else
                    ++it;
            }
        }

        // cache
        typedef boost::shared_mutex cache_mutex;
        typedef boost::shared_lock<cache_mutex> shared_lock;
        typedef boost::unique_lock<cache_mutex> unique_lock;
        typedef boost::unordered_map<ptr_pair, dp_t, boost::hash<ptr_pair>> Cache;
        cache_mutex mutex;

        dp_t p, dpre, dexp;
        std::atomic<unsigned> misses, hits;
        Cache cache;
    };

    cached_ddp _cached_ddp;
};
#endif // _OPENCOG_METAPOPULATION_H

} // ~namespace moses
} // ~namespace opencog
