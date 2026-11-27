# Product controls: capture and session

What capture and a session must do with the golfer's shots. Who owns a shot, a session and its folder, and where they persist, is the information model's (`governance/artefacts/information_model.md`). How the waiting dialog looks is the design system's ("Wait, or … anyway", `governance/artefacts/design_system/README.md`). An area reference: read it when doing this kind of work ([`AGENTS.md`](../../AGENTS.md) has the index). Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead); all four are the owner, so read a role's approval as "agree it with the owner first".

## A shot still being saved

**AD-50.** Ending a session or quitting the app must never silently drop a shot that is still being saved (between its impact and its saved file).
End and quit wait for the shot, and say so ("Saving the last shot…"); the session ends, or the app closes, as soon as the shot is saved. The golfer may choose to drop it instead ("End anyway", "Close anyway"); a session whose only shot is dropped is then empty and is discarded like any empty session. Releasing the cameras and sensors comes after the session has ended, never before.
*Why:* at the first hands-on visit an End pressed while a shot was analysing lost the shot, and a quit at the same moment lost the shot and its whole session (#280, #281, #188).[^ad-50]

[^ad-50]: Product Owner, 25 Nov 2026 (the redesign of End and quit during analysis; #280, #281).
