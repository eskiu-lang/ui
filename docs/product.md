# Product guide

What makes a ui app worth installing and worth keeping: what it does for the user, how it
speaks, how it earns trust, and how it presents itself in the stores and on the web. The
[design guide](design.md) covers how screens look and [app structure](app-structure.md) where
the code goes; this guide is about the decisions before and around them. It is written for
people and for coding agents building an app with someone.

## Know the job before the screens

Write down, before the first screen, in one sentence each: who the app is for, the one job it
does for them, and what they do today instead. Every screen should serve that job; a feature
that does not is a candidate to cut. When an agent builds an app from a request, it asks for
these three answers if the request does not give them, and states the assumption it makes
otherwise.

- Name the main action. On the home screen it is the most visible thing (the primary button,
  or the first item), and it is never more than one tap away.
- Count the steps of the main flow and remove the ones that do not change the outcome:
  defaults instead of questions, one screen instead of two, the last value remembered.
- Ship the smallest version that does the job completely, rather than many features halfway.

## The first minute

People decide in the first minute whether an app is worth keeping.

- Show value before asking for anything: no account, permission or tutorial before the user
  has seen what the app does. If an account is needed, let them try first and ask when there
  is something to save.
- The first screen is not empty: the empty state explains what goes there and has the action
  that fills it, or the app starts with an example the user can delete.
- Onboarding, if any, is three screens or fewer, each with one idea, and can be skipped. Better:
  teach in place, with a hint next to the control the first time it matters.
- The app opens fast. Keep the first screen light (it is prerendered on the web), load the rest
  after it shows, and hold the splash (`ui_splash`) only while saved state loads.

## Words

The app's words are part of its design (`src/words.esk`).

- Write as the user would say it: "Add a task", not "Create new entity". Use the same word for
  the same thing everywhere.
- Buttons say what happens, with a verb: "Save changes", "Send invite", "Delete task". Not
  "OK", "Submit" or "Yes".
- Titles are short and in sentence case. Explanations are one sentence.
- Errors say what happened and what to do, without blame or codes: "Couldn't save. Check your
  connection and try again." Keep a code, if one helps support, out of the main text.
- Confirmations are in the past tense and short: "Task deleted", with Undo when possible.
- Write every word in each language the app supports, not translated from the screen at the
  end: plan for longer words (German, Spanish) and for right-to-left languages.

## Permissions and notifications

- Ask for a permission at the moment it is needed, after the user taps what needs it, with a
  sentence of your own before the system's prompt saying what it is for ("To scan the code,
  allow the camera"). Never at launch.
- When a permission is refused, the feature says so in place and offers the settings, and the
  rest of the app keeps working.
- Notifications are for what the user asked to hear about. Ask to send them after the user has
  done something they would want to be told about (set a reminder), never on first launch.
  Each one is actionable: tapping it opens the exact screen it is about (`ui_link`).

## Trust and privacy

- Collect only what the job needs, and say why where you ask for it.
- Secrets (tokens, passwords) go in `ui-securestore`, never in plain storage or in logs.
- Let users export and delete their data from the settings, and sign out from one tap.
- No dark patterns: no pre-checked consent, no hidden cancel, no fake urgency, no shame on the
  decline button. An honest "No thanks" keeps users who would otherwise uninstall.
- A paid feature says its price and what it unlocks before the user commits, and the free
  part of the app stays useful.

## Measuring what matters

- Decide the few numbers that say the app does its job (the main action completed, a user
  coming back the next week) and measure those, not every tap.
- Count events without personal data, and say so in the privacy policy. Respect the
  platform's tracking choices.
- Watch where people stop in the main flow and fix that step before adding features.
- Read reviews and support messages; the same complaint three times is a priority.

## Retention without tricks

- Bring people back with value: the app remembers where they were (it saves the route and
  what was being edited through `model/storage.esk` and opens there again), a reminder they
  set, content that changed for them.
- Make returning fast: the last screen or the most useful one, not a home they must navigate.
- Ask for a store rating only after a success (a task finished, a goal reached), not at launch
  and not after an error, and not more than a few times a year.

## Performance is part of the product

- Cold start under a second on a recent phone; the first screen with real content, not a
  spinner, when the data is saved locally.
- Scrolling at the display's rate: virtual lists (`FlatList`) for long lists, images sized for
  their box.
- Work offline where it can: show what was loaded, queue what the user does, and say clearly
  what needs the network.
- `ui agent ... perf` and the budgets in CI tell when a change makes the app slower.

## The store and the web

The listing is the first screen most people see.

- **Name and subtitle**: the name is short and memorable; the subtitle says the job in a few
  words ("Tasks for busy teams"), with the words people search for.
- **Icon** (`ui.toml` `icon`): one simple shape on a solid background, readable at 29 points,
  without words or a screenshot inside. It looks right on light and dark home screens.
- **Screenshots**: the first two sell the app. Each shows one benefit with a short caption
  above a real, polished screen with realistic content (no lorem ipsum, no empty states,
  no debug data), in light mode, on the current phone sizes. Take them with
  `ui agent ios "screenshot ..."` from a demo account.
- **Description**: the first lines say what the app does and for whom; then the main
  features as plain sentences; then what makes it different. No superlatives without proof.
- **Splash** (`ui.toml` `[splash]`): the app's background color and its icon, matching the first
  screen, so the launch feels instant.
- **Web**: the web build's first screen is prerendered into the page, so it is what search
  engines and link previews see. Give it a real title and a description, and make the main
  routes real URLs that open the right screen (they do: routes are the URL on the web).
- **Sharing**: anything worth sharing has a link that opens it in the app or on the web
  (`ui_share`, `ui_link`), with a preview that makes sense to someone who does not have the app.

## Releases

- Release small and often; each release note says in one or two sentences what the user gets,
  not what changed in the code.
- Keep the version (`ui.toml` `version`) meaningful: a feature is a minor version, a fix a
  patch.
- Update images, sounds and other assets without a store release where it helps (`ui publish`),
  never to change what the reviewed app does.
- Before a release: the main flow's `.uitest` passes on every platform, the screenshots are
  current, and the app works on a slow network and offline.

## A product checklist

1. The job, the user and the main action written down; the home screen leads to it.
2. Value shown before any account, permission or onboarding.
3. Every screen's words read as the user would say them, in every language shipped.
4. Permissions and notifications asked in context, with an explanation, and the app still
   useful without them.
5. Loading, empty, error and offline states designed ([design guide](design.md#states)).
6. A few numbers measured, without personal data.
7. Store name, subtitle, icon, screenshots and description ready, and the web's first screen
   titled and described.
