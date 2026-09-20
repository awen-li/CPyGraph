#include "analysis/cg/on_the_fly.h"

#include "analysis/pta/interprocedural_pta.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cpygraph::cg {
namespace {

constexpr std::size_t kHashCombineConstant = 0x9e3779b9U;
constexpr std::size_t kHashLeftShift = 6U;
constexpr std::size_t kHashRightShift = 2U;

std::size_t combineHash(std::size_t first, std::size_t second) noexcept {
    return first ^ (second + kHashCombineConstant + (first << kHashLeftShift) +
                    (first >> kHashRightShift));
}

CalleeGroupId internGroup(std::vector<UnresolvedCalleeGroup>& groups,
                          const UnresolvedCalleeGroup& group) {
    const auto found = std::find(groups.begin(), groups.end(), group);
    if (found != groups.end())
        return static_cast<CalleeGroupId>(found - groups.begin() + 1U);
    groups.push_back(group);
    return static_cast<CalleeGroupId>(groups.size());
}

}  // namespace

OnTheFlyCallGraphBuilder::OnTheFlyCallGraphBuilder(
    PointerAnalysis& pointer_analysis) noexcept
    : pointer_analysis_(pointer_analysis) {}

std::size_t OnTheFlyCallGraphBuilder::TargetKeyHash::operator()(
    const TargetKey& target) const noexcept {
    return combineHash(std::hash<FunctionId>{}(target.function),
                       std::hash<CodeObjectId>{}(target.code));
}

std::size_t OnTheFlyCallGraphBuilder::EdgeKeyHash::operator()(
    const EdgeKey& edge) const noexcept {
    return combineHash(std::hash<std::uint64_t>{}(edge.site),
                       TargetKeyHash{}(edge.target));
}

std::size_t OnTheFlyCallGraphBuilder::BodyKeyHash::operator()(
    const BodyKey& body) const noexcept {
    return combineHash(TargetKeyHash{}(body.target),
                       std::hash<ContextId>{}(body.context));
}

void OnTheFlyCallGraphBuilder::registerCallable(ObjectId object,
                                                 CallableTarget target) {
    if (object == 0 || object == pointer_analysis_.unknownObject())
        throw std::invalid_argument("callable object id is reserved");
    if (target.function == 0 || target.code == 0)
        throw std::invalid_argument("callable target ids must be nonzero");
    const auto found = callables_.find(object);
    if (found != callables_.end() &&
        (found->second.function != target.function || found->second.code != target.code))
        throw std::invalid_argument(
            "callable object " + std::to_string(object) +
            " already targets function/code " +
            std::to_string(found->second.function) + "/" +
            std::to_string(found->second.code) +
            "; cannot retarget to " + std::to_string(target.function) + "/" +
            std::to_string(target.code));
    callables_.insert_or_assign(object, std::move(target));
}

void OnTheFlyCallGraphBuilder::assignCallable(ObjectId object,
                                               CallableTarget target) {
    if (object == 0 || object == pointer_analysis_.unknownObject())
        throw std::invalid_argument("callable object id is reserved");
    if (target.function == 0 || target.code == 0)
        throw std::invalid_argument("callable target ids must be nonzero");
    callables_.insert_or_assign(object, std::move(target));
}

void OnTheFlyCallGraphBuilder::addEntryPoint(
    CallableTarget target, CallableBodySummary body) {
    if (target.function == 0U || target.code == 0U)
        throw std::invalid_argument("entry-point target ids must be nonzero");
    const BodyKey key{{target.function, target.code}, 0U};
    if (bodies_.find(key) != bodies_.end()) return;
    auto sites = body.call_sites;
    addCallSites(std::move(sites));
    bodies_.emplace(key, std::move(body));
}

void OnTheFlyCallGraphBuilder::validateSite(const OnTheFlyCallSite& site) const {
    if (site.site.id == 0 || site.site.caller == 0 || site.site.callee_value == 0 ||
        site.call_result == 0)
        throw std::invalid_argument("on-the-fly call site contains a reserved id");
}

void OnTheFlyCallGraphBuilder::addCallSite(OnTheFlyCallSite site) {
    validateSite(site);
    const auto id = site.site.id;
    if (call_site_ids_.count(id) != 0U)
        throw std::invalid_argument("on-the-fly call-site ids must be unique");
    call_sites_.push_back(std::move(site));
    try {
        call_site_ids_.insert(id);
    } catch (...) {
        call_sites_.pop_back();
        throw;
    }
}

void OnTheFlyCallGraphBuilder::validateSites(
    const std::vector<OnTheFlyCallSite>& sites) const {
    std::unordered_set<std::uint64_t> batch_ids;
    batch_ids.reserve(sites.size());
    for (const auto& site : sites) {
        validateSite(site);
        if (call_site_ids_.count(site.site.id) != 0U ||
            !batch_ids.insert(site.site.id).second)
            throw std::invalid_argument("on-the-fly call-site ids must be unique");
    }
}

void OnTheFlyCallGraphBuilder::addCallSites(std::vector<OnTheFlyCallSite> sites) {
    validateSites(sites);
    for (auto& site : sites) addCallSite(std::move(site));
}

void OnTheFlyCallGraphBuilder::updateUnresolvedGroup(
    std::uint64_t site, UnresolvedCalleeGroup group) {
    if (site == 0U)
        throw std::invalid_argument("on-the-fly call-site id zero is reserved");
    const auto found = std::find_if(
        call_sites_.begin(), call_sites_.end(),
        [site](const auto& candidate) { return candidate.site.id == site; });
    if (found == call_sites_.end())
        throw std::out_of_range("unknown on-the-fly call-site id");
    found->site.unresolved_group = group;
}

const CallableBodySummary& OnTheFlyCallGraphBuilder::activate(
    CallableTarget target, ContextId context,
    const ContextualBodyProvider& provider, bool& changed) {
    const BodyKey key{{target.function, target.code}, context};
    const auto found = bodies_.find(key);
    if (found != bodies_.end()) return found->second;
    auto body = provider(target, context);
    auto nested_sites = body.call_sites;
    validateSites(nested_sites);
    const auto [inserted, accepted] = bodies_.emplace(key, std::move(body));
    if (!accepted) return inserted->second;
    addCallSites(std::move(nested_sites));
    changed = true;
    return inserted->second;
}

OnTheFlyCallGraphResult OnTheFlyCallGraphBuilder::build(
    const BodyProvider& body_provider, const BindingProvider& binding_provider) {
    if (!body_provider)
        throw std::invalid_argument("on-the-fly call graph requires a body provider");
    return build(
        [&](CallableTarget target, ContextId) {
            return body_provider(target);
        },
        [](const OnTheFlyCallSite&, CallableTarget) { return ContextId{0U}; },
        binding_provider);
}

OnTheFlyCallGraphResult OnTheFlyCallGraphBuilder::build(
    const ContextualBodyProvider& body_provider,
    const ContextSelector& context_selector,
    const BindingProvider& binding_provider,
    const UnresolvedSiteHandler& unresolved_site_handler) {
    if (!body_provider)
        throw std::invalid_argument("on-the-fly call graph requires a body provider");
    if (!context_selector)
        throw std::invalid_argument("on-the-fly call graph requires a context selector");

    std::vector<CallEdge> edges;
    std::vector<UnresolvedCalleeGroup> groups;
    std::unordered_set<EdgeKey, EdgeKeyHash> resolved_edges;
    bool revisit_unresolved = true;
    while (revisit_unresolved) {
        bool changed = true;
        while (changed) {
            changed = false;
            pointer_analysis_.solve();
            // Activation can append nested sites. Index iteration intentionally
            // sees them in this or the following fixed-point round.
            for (std::size_t site_index = 0; site_index < call_sites_.size();
                 ++site_index) {
                const auto site = call_sites_[site_index];
                const auto points_to =
                    pointer_analysis_.pointsTo(site.site.callee_value);
                for (const auto object : points_to) {
                    const auto callable = callables_.find(object);
                    if (callable == callables_.end()) continue;
                    // Body activation may register more callable objects and
                    // rehash callables_, so retain the target by value.
                    const auto target = callable->second;
                    const TargetKey target_key{target.function, target.code};
                    const auto context = context_selector(site, target);
                    const auto& body = activate(
                        target, context, body_provider, changed);
                    // Different closure objects can share a code target at one
                    // call site. Feed every object to the body's hidden capture
                    // input even though the CG contains one target edge.
                    if (body.capture_environment != 0U &&
                        capture_objects_[body.capture_environment].insert(object).second) {
                        pointer_analysis_.addAddressOf(
                            body.capture_environment, object);
                        changed = true;
                    }
                    if (!resolved_edges.insert({site.site.id, target_key}).second)
                        continue;

                    const auto bindings = binding_provider
                        ? binding_provider(site, target)
                        : std::vector<ArgumentBinding>{};
                    InterproceduralPTAStitcher(pointer_analysis_).stitch({{
                        site.actual_arguments,
                        body.formal_parameters,
                        body.return_values,
                        site.call_result,
                        bindings,
                    }});
                    edges.push_back({site.site.id, site.site.caller, target, 0U});
                    changed = true;
                }
            }
        }
        revisit_unresolved = false;
        if (unresolved_site_handler) {
            std::vector<std::uint64_t> unresolved_sites;
            for (const auto& site : call_sites_) {
                const auto& points_to =
                    pointer_analysis_.pointsTo(site.site.callee_value);
                bool unresolved = points_to.empty();
                for (const auto object : points_to)
                    if (callables_.find(object) == callables_.end())
                        unresolved = true;
                if (unresolved) unresolved_sites.push_back(site.site.id);
            }
            revisit_unresolved = unresolved_site_handler(unresolved_sites);
        }
    }

    // Unknown or non-callable alternatives remain explicit after the coupled
    // PTA/CG fixed point has no more reachable bodies to activate.
    for (const auto& site : call_sites_) {
        const auto& points_to = pointer_analysis_.pointsTo(site.site.callee_value);
        bool unresolved = points_to.empty();
        for (const auto object : points_to)
            if (callables_.find(object) == callables_.end()) unresolved = true;
        if (unresolved)
            edges.push_back({site.site.id, site.site.caller, std::nullopt,
                             internGroup(groups, site.site.unresolved_group)});
    }

    return {CallGraph(std::move(edges), std::move(groups)),
            call_sites_.size(), bodies_.size()};
}

}  // namespace cpygraph::cg
