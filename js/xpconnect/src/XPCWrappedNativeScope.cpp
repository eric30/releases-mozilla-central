/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Class used to manage the wrapped native objects within a JS scope. */

#include "xpcprivate.h"
#include "XPCWrapper.h"
#include "jsproxy.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsPrincipal.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Preferences.h"

#include "mozilla/dom/BindingUtils.h"

using namespace mozilla;
using namespace xpc;
using namespace JS;

/***************************************************************************/

#ifdef XPC_TRACK_SCOPE_STATS
static int DEBUG_TotalScopeCount;
static int DEBUG_TotalLiveScopeCount;
static int DEBUG_TotalMaxScopeCount;
static int DEBUG_TotalScopeTraversalCount;
static bool    DEBUG_DumpedStats;
#endif

#ifdef DEBUG
static void DEBUG_TrackNewScope(XPCWrappedNativeScope* scope)
{
#ifdef XPC_TRACK_SCOPE_STATS
    DEBUG_TotalScopeCount++;
    DEBUG_TotalLiveScopeCount++;
    if (DEBUG_TotalMaxScopeCount < DEBUG_TotalLiveScopeCount)
        DEBUG_TotalMaxScopeCount = DEBUG_TotalLiveScopeCount;
#endif
}

static void DEBUG_TrackDeleteScope(XPCWrappedNativeScope* scope)
{
#ifdef XPC_TRACK_SCOPE_STATS
    DEBUG_TotalLiveScopeCount--;
#endif
}

static void DEBUG_TrackScopeTraversal()
{
#ifdef XPC_TRACK_SCOPE_STATS
    DEBUG_TotalScopeTraversalCount++;
#endif
}

static void DEBUG_TrackScopeShutdown()
{
#ifdef XPC_TRACK_SCOPE_STATS
    if (!DEBUG_DumpedStats) {
        DEBUG_DumpedStats = true;
        printf("%d XPCWrappedNativeScope(s) were constructed.\n",
               DEBUG_TotalScopeCount);

        printf("%d XPCWrappedNativeScopes(s) max alive at one time.\n",
               DEBUG_TotalMaxScopeCount);

        printf("%d XPCWrappedNativeScope(s) alive now.\n" ,
               DEBUG_TotalLiveScopeCount);

        printf("%d traversals of Scope list.\n",
               DEBUG_TotalScopeTraversalCount);
    }
#endif
}
#else
#define DEBUG_TrackNewScope(scope) ((void)0)
#define DEBUG_TrackDeleteScope(scope) ((void)0)
#define DEBUG_TrackScopeTraversal() ((void)0)
#define DEBUG_TrackScopeShutdown() ((void)0)
#endif

/***************************************************************************/

XPCWrappedNativeScope* XPCWrappedNativeScope::gScopes = nullptr;
XPCWrappedNativeScope* XPCWrappedNativeScope::gDyingScopes = nullptr;

// static
XPCWrappedNativeScope*
XPCWrappedNativeScope::GetNewOrUsed(JSContext *cx, JS::HandleObject aGlobal)
{
    XPCWrappedNativeScope* scope = GetObjectScope(aGlobal);
    if (!scope) {
        scope = new XPCWrappedNativeScope(cx, aGlobal);
    }
    return scope;
}

static bool
RemoteXULForbidsXBLScope(nsIPrincipal *aPrincipal, HandleObject aGlobal)
{
  // Check for random JSD scopes that don't have a principal.
  if (!aPrincipal)
      return false;

  // The SafeJSContext is lazily created, and tends to be created at really
  // weird times, at least for xpcshell (often very early in startup or late
  // in shutdown). Its scope isn't system principal, so if we proceeded we'd
  // end up calling into AllowXULXBLForPrincipal, which depends on all kinds
  // of persistent storage and permission machinery that may or not be running.
  // We know the answer to the question here, so just short-circuit.
  if (JS_GetClass(aGlobal) == &SafeJSContextGlobalClass)
      return false;

  // AllowXULXBLForPrincipal will return true for system principal, but we
  // don't want that here.
  MOZ_ASSERT(nsContentUtils::IsInitialized());
  if (nsContentUtils::IsSystemPrincipal(aPrincipal))
      return false;

  // If this domain isn't whitelisted, we're done.
  if (!nsContentUtils::AllowXULXBLForPrincipal(aPrincipal))
      return false;

  // Check the pref to determine how we should behave.
  return !Preferences::GetBool("dom.use_xbl_scopes_for_remote_xul", false);
}

XPCWrappedNativeScope::XPCWrappedNativeScope(JSContext *cx,
                                             JS::HandleObject aGlobal)
      : mWrappedNativeMap(Native2WrappedNativeMap::newMap(XPC_NATIVE_MAP_SIZE)),
        mWrappedNativeProtoMap(ClassInfo2WrappedNativeProtoMap::newMap(XPC_NATIVE_PROTO_MAP_SIZE)),
        mMainThreadWrappedNativeProtoMap(ClassInfo2WrappedNativeProtoMap::newMap(XPC_NATIVE_PROTO_MAP_SIZE)),
        mComponents(nullptr),
        mNext(nullptr),
        mGlobalJSObject(aGlobal),
        mIsXBLScope(false)
{
    // add ourselves to the scopes list
    {
        MOZ_ASSERT(aGlobal);
        MOZ_ASSERT(js::GetObjectClass(aGlobal)->flags & (JSCLASS_PRIVATE_IS_NSISUPPORTS |
                                                         JSCLASS_HAS_PRIVATE)); 
        // scoped lock
        XPCAutoLock lock(XPCJSRuntime::Get()->GetMapLock());

#ifdef DEBUG
        for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext)
            MOZ_ASSERT(aGlobal != cur->GetGlobalJSObjectPreserveColor(), "dup object");
#endif

        mNext = gScopes;
        gScopes = this;

        // Grab the XPCContext associated with our context.
        mContext = XPCContext::GetXPCContext(cx);
        mContext->AddScope(this);
    }

    DEBUG_TrackNewScope(this);
    MOZ_COUNT_CTOR(XPCWrappedNativeScope);

    // Attach ourselves to the compartment private.
    CompartmentPrivate *priv = EnsureCompartmentPrivate(aGlobal);
    priv->scope = this;

    // Determine whether we would allow an XBL scope in this situation.
    // In addition to being pref-controlled, we also disable XBL scopes for
    // remote XUL domains, _except_ if we have an additional pref override set.
    nsIPrincipal *principal = GetPrincipal();
    mAllowXBLScope = !RemoteXULForbidsXBLScope(principal, aGlobal);

    // Determine whether to use an XBL scope.
    mUseXBLScope = mAllowXBLScope;
    if (mUseXBLScope) {
      js::Class *clasp = js::GetObjectClass(mGlobalJSObject);
      mUseXBLScope = !strcmp(clasp->name, "Window") ||
                     !strcmp(clasp->name, "ChromeWindow") ||
                     !strcmp(clasp->name, "ModalContentWindow");
    }
    if (mUseXBLScope) {
      mUseXBLScope = principal && !nsContentUtils::IsSystemPrincipal(principal);
    }
}

// static
bool
XPCWrappedNativeScope::IsDyingScope(XPCWrappedNativeScope *scope)
{
    for (XPCWrappedNativeScope *cur = gDyingScopes; cur; cur = cur->mNext) {
        if (scope == cur)
            return true;
    }
    return false;
}

JSObject*
XPCWrappedNativeScope::GetComponentsJSObject()
{
    AutoJSContext cx;
    if (!mComponents)
        mComponents = new nsXPCComponents(this);

    AutoMarkingNativeInterfacePtr iface(cx);
    iface = XPCNativeInterface::GetNewOrUsed(&NS_GET_IID(nsIXPCComponents));
    if (!iface)
        return nullptr;

    nsCOMPtr<nsIXPCComponents> cholder(mComponents);
    xpcObjectHelper helper(cholder);
    nsCOMPtr<XPCWrappedNative> wrapper;
    XPCWrappedNative::GetNewOrUsed(helper, this, iface, getter_AddRefs(wrapper));
    if (!wrapper)
        return nullptr;

    // The call to wrap() here is necessary even though the object is same-
    // compartment, because it applies our security wrapper.
    JS::RootedObject obj(cx, wrapper->GetFlatJSObject());
    if (!JS_WrapObject(cx, obj.address()))
        return nullptr;
    return obj;
}

JSObject*
XPCWrappedNativeScope::EnsureXBLScope(JSContext *cx)
{
    JS::RootedObject global(cx, GetGlobalJSObject());
    MOZ_ASSERT(js::IsObjectInContextCompartment(global, cx));
    MOZ_ASSERT(!mIsXBLScope);
    MOZ_ASSERT(strcmp(js::GetObjectClass(global)->name,
                      "nsXBLPrototypeScript compilation scope"));

    // If we already have a special XBL scope object, we know what to use.
    if (mXBLScope)
        return mXBLScope;

    // If this scope doesn't need an XBL scope, just return the global.
    if (!mUseXBLScope)
        return global;

    // Set up the sandbox options. Note that we use the DOM global as the
    // sandboxPrototype so that the XBL scope can access all the DOM objects
    // it's accustomed to accessing.
    //
    // NB: One would think that wantXrays wouldn't make a difference here.
    // However, wantXrays lives a secret double life, and one of its other
    // hobbies is to waive Xray on the returned sandbox when set to false.
    // So make sure to keep this set to true, here.
    SandboxOptions options(cx);
    options.wantXrays = true;
    options.wantComponents = true;
    options.proto = global;
    options.sameZoneAs = global;

    // Use an nsExpandedPrincipal to create asymmetric security.
    nsIPrincipal *principal = GetPrincipal();
    nsCOMPtr<nsIExpandedPrincipal> ep;
    MOZ_ASSERT(!(ep = do_QueryInterface(principal)));
    nsTArray< nsCOMPtr<nsIPrincipal> > principalAsArray(1);
    principalAsArray.AppendElement(principal);
    ep = new nsExpandedPrincipal(principalAsArray);

    // Create the sandbox.
    JS::RootedValue v(cx, JS::UndefinedValue());
    nsresult rv = CreateSandboxObject(cx, v.address(), ep, options);
    NS_ENSURE_SUCCESS(rv, nullptr);
    mXBLScope = &v.toObject();

    // Tag it.
    EnsureCompartmentPrivate(js::UncheckedUnwrap(mXBLScope))->scope->mIsXBLScope = true;

    // Good to go!
    return mXBLScope;
}

bool
XPCWrappedNativeScope::AllowXBLScope()
{
    // We only disallow XBL scopes in remote XUL situations.
    MOZ_ASSERT_IF(!mAllowXBLScope,
                  nsContentUtils::AllowXULXBLForPrincipal(GetPrincipal()));
    return mAllowXBLScope;
}

namespace xpc {
JSObject *GetXBLScope(JSContext *cx, JSObject *contentScopeArg)
{
    JS::RootedObject contentScope(cx, contentScopeArg);
    JSAutoCompartment ac(cx, contentScope);
    JSObject *scope = EnsureCompartmentPrivate(contentScope)->scope->EnsureXBLScope(cx);
    NS_ENSURE_TRUE(scope, nullptr); // See bug 858642.
    scope = js::UncheckedUnwrap(scope);
    xpc_UnmarkGrayObject(scope);
    return scope;
}

bool AllowXBLScope(JSCompartment *c)
{
  XPCWrappedNativeScope *scope = EnsureCompartmentPrivate(c)->scope;
  return scope && scope->AllowXBLScope();
}
} /* namespace xpc */

XPCWrappedNativeScope::~XPCWrappedNativeScope()
{
    MOZ_COUNT_DTOR(XPCWrappedNativeScope);
    DEBUG_TrackDeleteScope(this);

    // We can do additional cleanup assertions here...

    if (mWrappedNativeMap) {
        MOZ_ASSERT(0 == mWrappedNativeMap->Count(), "scope has non-empty map");
        delete mWrappedNativeMap;
    }

    if (mWrappedNativeProtoMap) {
        MOZ_ASSERT(0 == mWrappedNativeProtoMap->Count(), "scope has non-empty map");
        delete mWrappedNativeProtoMap;
    }

    if (mMainThreadWrappedNativeProtoMap) {
        MOZ_ASSERT(0 == mMainThreadWrappedNativeProtoMap->Count(), "scope has non-empty map");
        delete mMainThreadWrappedNativeProtoMap;
    }

    if (mContext)
        mContext->RemoveScope(this);

    // This should not be necessary, since the Components object should die
    // with the scope but just in case.
    if (mComponents)
        mComponents->mScope = nullptr;

    // XXX we should assert that we are dead or that xpconnect has shutdown
    // XXX might not want to do this at xpconnect shutdown time???
    mComponents = nullptr;

    JSRuntime *rt = XPCJSRuntime::Get()->Runtime();
    mXBLScope.finalize(rt);
    mGlobalJSObject.finalize(rt);
}

static PLDHashOperator
WrappedNativeJSGCThingTracer(PLDHashTable *table, PLDHashEntryHdr *hdr,
                             uint32_t number, void *arg)
{
    XPCWrappedNative* wrapper = ((Native2WrappedNativeMap::Entry*)hdr)->value;
    if (wrapper->HasExternalReference() && !wrapper->IsWrapperExpired())
        wrapper->TraceSelf((JSTracer *)arg);

    return PL_DHASH_NEXT;
}

// static
void
XPCWrappedNativeScope::TraceWrappedNativesInAllScopes(JSTracer* trc, XPCJSRuntime* rt)
{
    // FIXME The lock may not be necessary during tracing as that serializes
    // access to JS runtime. See bug 380139.
    XPCAutoLock lock(rt->GetMapLock());

    // Do JS_CallTracer for all wrapped natives with external references, as
    // well as any DOM expando objects.
    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext) {
        cur->mWrappedNativeMap->Enumerate(WrappedNativeJSGCThingTracer, trc);
        if (cur->mDOMExpandoSet) {
            for (DOMExpandoSet::Enum e(*cur->mDOMExpandoSet); !e.empty(); e.popFront())
                JS_CallHashSetObjectTracer(trc, e, e.front(), "DOM expando object");
        }
    }
}

static PLDHashOperator
WrappedNativeSuspecter(PLDHashTable *table, PLDHashEntryHdr *hdr,
                       uint32_t number, void *arg)
{
    XPCWrappedNative* wrapper = ((Native2WrappedNativeMap::Entry*)hdr)->value;

    if (wrapper->HasExternalReference()) {
        nsCycleCollectionNoteRootCallback *cb =
            static_cast<nsCycleCollectionNoteRootCallback *>(arg);
        XPCJSRuntime::SuspectWrappedNative(wrapper, *cb);
    }

    return PL_DHASH_NEXT;
}

static void
SuspectDOMExpandos(JSObject *obj, nsCycleCollectionNoteRootCallback &cb)
{
    MOZ_ASSERT(dom::GetDOMClass(obj) && dom::GetDOMClass(obj)->mDOMObjectIsISupports);
    nsISupports* native = dom::UnwrapDOMObject<nsISupports>(obj);
    cb.NoteXPCOMRoot(native);
}

// static
void
XPCWrappedNativeScope::SuspectAllWrappers(XPCJSRuntime* rt,
                                          nsCycleCollectionNoteRootCallback& cb)
{
    XPCAutoLock lock(rt->GetMapLock());

    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext) {
        cur->mWrappedNativeMap->Enumerate(WrappedNativeSuspecter, &cb);
        if (cur->mDOMExpandoSet) {
            for (DOMExpandoSet::Range r = cur->mDOMExpandoSet->all(); !r.empty(); r.popFront())
                SuspectDOMExpandos(r.front(), cb);
        }
    }
}

// static
void
XPCWrappedNativeScope::StartFinalizationPhaseOfGC(JSFreeOp *fop, XPCJSRuntime* rt)
{
    // FIXME The lock may not be necessary since we are inside JSGC_MARK_END
    // callback and GX serializes access to JS runtime. See bug 380139.
    XPCAutoLock lock(rt->GetMapLock());

    // We are in JSGC_MARK_END and JSGC_FINALIZE_END must always follow it
    // calling FinishedFinalizationPhaseOfGC and clearing gDyingScopes in
    // KillDyingScopes.
    MOZ_ASSERT(!gDyingScopes, "JSGC_MARK_END without JSGC_FINALIZE_END");

    XPCWrappedNativeScope* prev = nullptr;
    XPCWrappedNativeScope* cur = gScopes;

    while (cur) {
        // Sweep waivers.
        if (cur->mWaiverWrapperMap)
            cur->mWaiverWrapperMap->Sweep();

        XPCWrappedNativeScope* next = cur->mNext;

        if (cur->mGlobalJSObject && cur->mGlobalJSObject.isAboutToBeFinalized()) {
            cur->mGlobalJSObject.finalize(fop->runtime());
            // Move this scope from the live list to the dying list.
            if (prev)
                prev->mNext = next;
            else
                gScopes = next;
            cur->mNext = gDyingScopes;
            gDyingScopes = cur;
            cur = nullptr;
        }
        if (cur)
            prev = cur;
        cur = next;
    }
}

// static
void
XPCWrappedNativeScope::FinishedFinalizationPhaseOfGC()
{
    XPCJSRuntime* rt = nsXPConnect::GetRuntimeInstance();

    // FIXME The lock may not be necessary since we are inside
    // JSGC_FINALIZE_END callback and at this point GC still serializes access
    // to JS runtime. See bug 380139.
    XPCAutoLock lock(rt->GetMapLock());
    KillDyingScopes();
}

static PLDHashOperator
WrappedNativeMarker(PLDHashTable *table, PLDHashEntryHdr *hdr,
                    uint32_t number_t, void *arg)
{
    ((Native2WrappedNativeMap::Entry*)hdr)->value->Mark();
    return PL_DHASH_NEXT;
}

// We need to explicitly mark all the protos too because some protos may be
// alive in the hashtable but not currently in use by any wrapper
static PLDHashOperator
WrappedNativeProtoMarker(PLDHashTable *table, PLDHashEntryHdr *hdr,
                         uint32_t number, void *arg)
{
    ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value->Mark();
    return PL_DHASH_NEXT;
}

// static
void
XPCWrappedNativeScope::MarkAllWrappedNativesAndProtos()
{
    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext) {
        cur->mWrappedNativeMap->Enumerate(WrappedNativeMarker, nullptr);
        cur->mWrappedNativeProtoMap->Enumerate(WrappedNativeProtoMarker, nullptr);
        cur->mMainThreadWrappedNativeProtoMap->Enumerate(WrappedNativeProtoMarker, nullptr);
    }

    DEBUG_TrackScopeTraversal();
}

#ifdef DEBUG
static PLDHashOperator
ASSERT_WrappedNativeSetNotMarked(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 uint32_t number, void *arg)
{
    ((Native2WrappedNativeMap::Entry*)hdr)->value->ASSERT_SetsNotMarked();
    return PL_DHASH_NEXT;
}

static PLDHashOperator
ASSERT_WrappedNativeProtoSetNotMarked(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                      uint32_t number, void *arg)
{
    ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value->ASSERT_SetNotMarked();
    return PL_DHASH_NEXT;
}

// static
void
XPCWrappedNativeScope::ASSERT_NoInterfaceSetsAreMarked()
{
    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext) {
        cur->mWrappedNativeMap->Enumerate(ASSERT_WrappedNativeSetNotMarked, nullptr);
        cur->mWrappedNativeProtoMap->Enumerate(ASSERT_WrappedNativeProtoSetNotMarked, nullptr);
        cur->mMainThreadWrappedNativeProtoMap->Enumerate(ASSERT_WrappedNativeProtoSetNotMarked, nullptr);
    }
}
#endif

static PLDHashOperator
WrappedNativeTearoffSweeper(PLDHashTable *table, PLDHashEntryHdr *hdr,
                            uint32_t number, void *arg)
{
    ((Native2WrappedNativeMap::Entry*)hdr)->value->SweepTearOffs();
    return PL_DHASH_NEXT;
}

// static
void
XPCWrappedNativeScope::SweepAllWrappedNativeTearOffs()
{
    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext)
        cur->mWrappedNativeMap->Enumerate(WrappedNativeTearoffSweeper, nullptr);

    DEBUG_TrackScopeTraversal();
}

// static
void
XPCWrappedNativeScope::KillDyingScopes()
{
    // always called inside the lock!
    XPCWrappedNativeScope* cur = gDyingScopes;
    while (cur) {
        XPCWrappedNativeScope* next = cur->mNext;
        delete cur;
        cur = next;
    }
    gDyingScopes = nullptr;
}

struct ShutdownData
{
    ShutdownData()
        : wrapperCount(0),
          protoCount(0) {}
    int wrapperCount;
    int protoCount;
};

static PLDHashOperator
WrappedNativeShutdownEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                uint32_t number, void *arg)
{
    ShutdownData* data = (ShutdownData*) arg;
    XPCWrappedNative* wrapper = ((Native2WrappedNativeMap::Entry*)hdr)->value;

    if (wrapper->IsValid()) {
        wrapper->SystemIsBeingShutDown();
        data->wrapperCount++;
    }
    return PL_DHASH_REMOVE;
}

static PLDHashOperator
WrappedNativeProtoShutdownEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                     uint32_t number, void *arg)
{
    ShutdownData* data = (ShutdownData*) arg;
    ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value->
        SystemIsBeingShutDown();
    data->protoCount++;
    return PL_DHASH_REMOVE;
}

//static
void
XPCWrappedNativeScope::SystemIsBeingShutDown()
{
    DEBUG_TrackScopeTraversal();
    DEBUG_TrackScopeShutdown();

    int liveScopeCount = 0;

    ShutdownData data;

    XPCWrappedNativeScope* cur;

    // First move all the scopes to the dying list.

    cur = gScopes;
    while (cur) {
        XPCWrappedNativeScope* next = cur->mNext;
        cur->mNext = gDyingScopes;
        gDyingScopes = cur;
        cur = next;
        liveScopeCount++;
    }
    gScopes = nullptr;

    // We're forcibly killing scopes, rather than allowing them to go away
    // when they're ready. As such, we need to do some cleanup before they
    // can safely be destroyed.

    for (cur = gDyingScopes; cur; cur = cur->mNext) {
        // Give the Components object a chance to try to clean up.
        if (cur->mComponents)
            cur->mComponents->SystemIsBeingShutDown();

        // Walk the protos first. Wrapper shutdown can leave dangling
        // proto pointers in the proto map.
        cur->mWrappedNativeProtoMap->
                Enumerate(WrappedNativeProtoShutdownEnumerator,  &data);
        cur->mMainThreadWrappedNativeProtoMap->
                Enumerate(WrappedNativeProtoShutdownEnumerator,  &data);
        cur->mWrappedNativeMap->
                Enumerate(WrappedNativeShutdownEnumerator,  &data);
    }

    // Now it is safe to kill all the scopes.
    KillDyingScopes();

#ifdef XPC_DUMP_AT_SHUTDOWN
    if (data.wrapperCount)
        printf("deleting nsXPConnect  with %d live XPCWrappedNatives\n",
               data.wrapperCount);
    if (data.protoCount)
        printf("deleting nsXPConnect  with %d live XPCWrappedNativeProtos\n",
               data.protoCount);
    if (liveScopeCount)
        printf("deleting nsXPConnect  with %d live XPCWrappedNativeScopes\n",
               liveScopeCount);
#endif
}


/***************************************************************************/

static PLDHashOperator
WNProtoSecPolicyClearer(PLDHashTable *table, PLDHashEntryHdr *hdr,
                        uint32_t number, void *arg)
{
    XPCWrappedNativeProto* proto =
        ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value;
    *(proto->GetSecurityInfoAddr()) = nullptr;
    return PL_DHASH_NEXT;
}

// static
nsresult
XPCWrappedNativeScope::ClearAllWrappedNativeSecurityPolicies()
{
    // Hold the lock throughout.
    XPCAutoLock lock(XPCJSRuntime::Get()->GetMapLock());

    for (XPCWrappedNativeScope* cur = gScopes; cur; cur = cur->mNext) {
        cur->mWrappedNativeProtoMap->Enumerate(WNProtoSecPolicyClearer, nullptr);
        cur->mMainThreadWrappedNativeProtoMap->Enumerate(WNProtoSecPolicyClearer, nullptr);
    }

    DEBUG_TrackScopeTraversal();

    return NS_OK;
}

static PLDHashOperator
WNProtoRemover(PLDHashTable *table, PLDHashEntryHdr *hdr,
               uint32_t number, void *arg)
{
    XPCWrappedNativeProtoMap* detachedMap = (XPCWrappedNativeProtoMap*)arg;

    XPCWrappedNativeProto* proto = (XPCWrappedNativeProto*)
        ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value;

    detachedMap->Add(proto);

    return PL_DHASH_REMOVE;
}

void
XPCWrappedNativeScope::RemoveWrappedNativeProtos()
{
    XPCAutoLock al(XPCJSRuntime::Get()->GetMapLock());

    mWrappedNativeProtoMap->Enumerate(WNProtoRemover,
                                      GetRuntime()->GetDetachedWrappedNativeProtoMap());
    mMainThreadWrappedNativeProtoMap->Enumerate(WNProtoRemover,
                                                GetRuntime()->GetDetachedWrappedNativeProtoMap());
}

/***************************************************************************/

// static
void
XPCWrappedNativeScope::DebugDumpAllScopes(int16_t depth)
{
#ifdef DEBUG
    depth-- ;

    // get scope count.
    int count = 0;
    XPCWrappedNativeScope* cur;
    for (cur = gScopes; cur; cur = cur->mNext)
        count++ ;

    XPC_LOG_ALWAYS(("chain of %d XPCWrappedNativeScope(s)", count));
    XPC_LOG_INDENT();
        XPC_LOG_ALWAYS(("gDyingScopes @ %x", gDyingScopes));
        if (depth)
            for (cur = gScopes; cur; cur = cur->mNext)
                cur->DebugDump(depth);
    XPC_LOG_OUTDENT();
#endif
}

#ifdef DEBUG
static PLDHashOperator
WrappedNativeMapDumpEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                               uint32_t number, void *arg)
{
    ((Native2WrappedNativeMap::Entry*)hdr)->value->DebugDump(*(int16_t*)arg);
    return PL_DHASH_NEXT;
}
static PLDHashOperator
WrappedNativeProtoMapDumpEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                    uint32_t number, void *arg)
{
    ((ClassInfo2WrappedNativeProtoMap::Entry*)hdr)->value->DebugDump(*(int16_t*)arg);
    return PL_DHASH_NEXT;
}
#endif

void
XPCWrappedNativeScope::DebugDump(int16_t depth)
{
#ifdef DEBUG
    depth-- ;
    XPC_LOG_ALWAYS(("XPCWrappedNativeScope @ %x", this));
    XPC_LOG_INDENT();
        XPC_LOG_ALWAYS(("mNext @ %x", mNext));
        XPC_LOG_ALWAYS(("mComponents @ %x", mComponents.get()));
        XPC_LOG_ALWAYS(("mGlobalJSObject @ %x", mGlobalJSObject.get()));

        XPC_LOG_ALWAYS(("mWrappedNativeMap @ %x with %d wrappers(s)",         \
                        mWrappedNativeMap,                                    \
                        mWrappedNativeMap ? mWrappedNativeMap->Count() : 0));
        // iterate contexts...
        if (depth && mWrappedNativeMap && mWrappedNativeMap->Count()) {
            XPC_LOG_INDENT();
            mWrappedNativeMap->Enumerate(WrappedNativeMapDumpEnumerator, &depth);
            XPC_LOG_OUTDENT();
        }

        XPC_LOG_ALWAYS(("mWrappedNativeProtoMap @ %x with %d protos(s)",      \
                        mWrappedNativeProtoMap,                               \
                        mWrappedNativeProtoMap ? mWrappedNativeProtoMap->Count() : 0));
        // iterate contexts...
        if (depth && mWrappedNativeProtoMap && mWrappedNativeProtoMap->Count()) {
            XPC_LOG_INDENT();
            mWrappedNativeProtoMap->Enumerate(WrappedNativeProtoMapDumpEnumerator, &depth);
            XPC_LOG_OUTDENT();
        }

        XPC_LOG_ALWAYS(("mMainThreadWrappedNativeProtoMap @ %x with %d protos(s)", \
                        mMainThreadWrappedNativeProtoMap,                     \
                        mMainThreadWrappedNativeProtoMap ? mMainThreadWrappedNativeProtoMap->Count() : 0));
        // iterate contexts...
        if (depth && mMainThreadWrappedNativeProtoMap && mMainThreadWrappedNativeProtoMap->Count()) {
            XPC_LOG_INDENT();
            mMainThreadWrappedNativeProtoMap->Enumerate(WrappedNativeProtoMapDumpEnumerator, &depth);
            XPC_LOG_OUTDENT();
        }
    XPC_LOG_OUTDENT();
#endif
}

size_t
XPCWrappedNativeScope::SizeOfAllScopesIncludingThis(MallocSizeOf mallocSizeOf)
{
    XPCJSRuntime *rt = nsXPConnect::GetRuntimeInstance();
    XPCAutoLock lock(rt->GetMapLock());

    size_t n = 0;
    for (XPCWrappedNativeScope *cur = gScopes; cur; cur = cur->mNext) {
        n += cur->SizeOfIncludingThis(mallocSizeOf);
    }
    return n;
}

size_t
XPCWrappedNativeScope::SizeOfIncludingThis(MallocSizeOf mallocSizeOf)
{
    size_t n = 0;
    n += mallocSizeOf(this);
    n += mWrappedNativeMap->SizeOfIncludingThis(mallocSizeOf);
    n += mWrappedNativeProtoMap->SizeOfIncludingThis(mallocSizeOf);
    n += mMainThreadWrappedNativeProtoMap->SizeOfIncludingThis(mallocSizeOf);

    // There are other XPCWrappedNativeScope members that could be measured;
    // the above ones have been seen by DMD to be worth measuring.  More stuff
    // may be added later.

    return n;
}
