# Contributing to the GVSoC PULP models

Thank you for your interest in GVSoC. This guide explains how to report bugs,
request features, propose changes, and work with the maintainers of the
models.

Each model in this repository has a maintainer, and each maintainer can set
their own rules for the code they maintain (see [Maintainers](#maintainers)).
Maintainers who have not written their own rules follow the
[default guidelines](#default-maintainer-guidelines) at the end of this file.

## Reporting bugs

Open an issue. A good report lets the maintainer reproduce the problem without
having to ask for more information. Please include:

- the target and the exact command line (`gvrun --target ...`),
- the commit of the SDK and of this repository,
- what you expected and what happened instead, with the error message or a
  trace excerpt,
- if possible, a minimal application or test that shows the problem.

## Requesting features

Open an issue that describes what you need and why: the hardware behaviour to
model, your use case, and a link to the RTL or the specification if there is
one. Discussing a feature before implementing it avoids work that cannot be
merged.

## Proposing changes

Pull requests (merge requests on GitLab) are welcome. For anything larger than
a small fix, open an issue first and agree on the approach with the
maintainer.

- Keep each pull request focused on a single topic, and split unrelated
  changes into separate pull requests.
- Follow the commit message style of the repository: a short subject that
  starts with the model name (`idma_v3: ...`), followed by what changed and
  why.
- Keep the Apache 2.0 license header in new files. By contributing, you agree
  that your contribution is licensed under the [LICENSE](LICENSE) of this
  repository.

### AI-generated contributions

AI-generated contributions are generally welcome, but check with the
maintainer of the model first: some maintainers prefer an issue instead. You
remain responsible for the change: you must understand every line, have tested
it, and be able to answer review comments yourself.

## Maintainers

The maintainer of each file is listed in [.github/CODEOWNERS](.github/CODEOWNERS).
Maintainers are asked to review the pull requests that touch their files.

A maintainer can write their own rules in `.github/maintainers/<handle>.md`.
Before contributing to a model, check whether its maintainer has such a page;
if so, its rules take precedence over the default guidelines below.

| Maintainer | Rules |
|---|---|
| @haugoug | [.github/maintainers/haugoug.md](.github/maintainers/haugoug.md) |

Maintainers who are not listed in this table follow the default guidelines.

To add your own rules, create your page, add it to this table, and list
yourself as its owner in CODEOWNERS.

## Default maintainer guidelines

These guidelines apply to every maintainer who has not written their own
rules.

- Issues and pull requests are answered on a best-effort basis. If you have
  not received an answer after one week, feel free to post a polite reminder
  in the thread.
- The maintainer decides whether a change fits the model. A change may be
  refused, or accepted only after rework, even if it works.
- There is no global coding style. Contributions should follow the style of
  the model they change.
- The maintainer may ask you to split a pull request or to adapt your code to
  the style of the model.
- Models should stay faithful to the hardware they model. In the pull request,
  explain what behaviour the change reproduces and how you checked it (RTL,
  datasheet, measurements).
- Pull requests whose author has been inactive for three months may be closed.
  They can be reopened later.
