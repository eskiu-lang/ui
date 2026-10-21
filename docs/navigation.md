# Navigation

An app moves between screens on the phone and between pages on the web with the same code:
a `Router` holds a stack of routes, and a `Navigator` draws it. `examples/navigation` is a
travel app: a list of places, each place's page and a plan for the trip.

```eskiu
*Router router;

void ui_app_start(int which) {
    router = router_new("/");
    ui_mount(void() {
        Navigator(router, void(string route) {
            List<String> params;
            defer route_params_free(&params);
            if (route_match("/places/:id", route, &params)) { PlacePage(atoi(params.data[0].data)); }
            else { PlaceList(); }
        });
    });
}

void ui_app_stop() { router.free(); }
```

## Routes

A route is a path: `"/"`, `"/places/2"`, `"/places/2/plan?budget=comfort"`. The app decides what
each one shows. `route_match(pattern, route, &params)` matches a route against a pattern
segment by segment: a `:name` segment takes any one segment into `params`, a `*` takes the
rest, and the query is ignored. `route_params_free` frees what it put there.

## The router

`router_new(root)` makes a router with one route, owned by the app's model like
`signal_new`. It outlives the screens; the app frees it in `ui_app_stop`.

| Call | What it does |
|---|---|
| `router.push(route)` | Opens `route` over the current one. |
| `router.back()` | Back one route; false at the root. |
| `router.replace(route)` | Changes the top route, without a new history entry. |
| `router.reset(route)` | Only `route` on the stack. |
| `router.path()`, `router.depth()`, `router.at(i)` | The top route, how many there are, the one at depth `i` (0 is the root). Reading them subscribes. |

## The navigator

`Navigator(router, screen)` draws each route on the stack with `screen(route)`, each as a
screen that fills the navigator. Only the top one shows; the ones under it stay mounted and
keep their state, so going back finds the list as it was left.

On iOS and Android a pushed screen slides in from the right over the one below, and a
popped one slides out. On iOS the swipe from the left edge is interactive: the screen follows
the finger over the one below and goes back when let go past a third of the width (or
flicked), as UIKit's navigation does. Back comes from:

- the router (`router.back()`, a Back button the app draws);
- Android's back button and gesture, and iOS's swipe from the left edge;
- the browser's back and forward buttons on the web.

At the root, Android's back leaves the app as usual.

## The platform

The first `Navigator` mounted follows the platform:

- **The web.** The page's URL is the top route: a push adds a history entry, a replace
  changes it, back goes back, and the browser's back and forward move the router. Opening a
  URL (or reloading a page) opens its route over the root. Routes are paths under the page's
  `<base>`, `/` unless `ui.toml` says otherwise (`[web] base = "/places/"`); a server hosting
  the app returns `index.html` for paths that are not files, as `ui run web`'s does.
- **iOS and Android.** A link the app is opened with, or is sent while it runs, opens its
  route: `places://places/2` opens `/places/2` (the link's host is the route's first part,
  for a custom scheme), and `https://example.com/places/2` opens `/places/2`. The scheme
  comes from `ui.toml`:

```toml
[app]
scheme = "places"
```

On iOS it is a `CFBundleURLTypes` entry; on Android, an intent filter on the activity,
which is `singleTask` so a link reaches the running app.

## Tabs

Tabs are screens of their own: a `TabBar` (from `<ui/kit>`) over the selected tab's content,
each tab with its own router and `Navigator` if it has a stack. Only the first Navigator
follows the URL and links, so give that role to the tabs' outer router when there is one.

## Testing

`tests/model/navigation.esk` checks the stack, the screens kept under the top, back, links,
the history commands and the browser's route events. `examples/navigation` runs on every
platform. To try a link on the simulator without the system's confirmation, the iOS host
takes `--link URL`: `xcrun simctl launch booted APP_ID --link places://places/2`; on
Android, `adb shell am start -a android.intent.action.VIEW -d places://places/2`.
