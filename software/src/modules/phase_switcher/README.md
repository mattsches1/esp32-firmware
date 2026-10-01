# Emulate Energy Manager Patch

Emulate Energy Manager mode requires CM Networking hooks. Apply patch from the `software` directory:

```sh
./lib-builder/apply_patches.py . patches/firmware
```

The patch runner applies `git am` patches and creates a separate commit in this repository. Commit or stash other work first. Check whether this patch was already applied:

```sh
git log --oneline --grep='CM Networking: add Emulate Energy Manager hooks'
```

Do not apply the patch twice. If `git am` stops on a conflict, resolve the files and run `git am --continue`, or cancel with `git am --abort`.

When updating from upstream, rebase this patch commit onto the updated upstream branch. Resolve conflicts if upstream changed `cm_protocol.cpp` or `module.ini`, then verify the generated module dependencies and build. Keep this patch commit separate from phase-switcher module changes.