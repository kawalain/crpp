# Locales are compiled into the exe

Every string on screen was an inline Korean literal. Wording, and the functions that write times and counts, now live in a pure `locale` module with one source file per Locale, compiled into the exe. A release is one signed exe, so an outside language file would need a signature, a fetch and a failure path of its own, while a table in the exe costs little memory and cannot be missing. A test checks that every Locale has every key and the same placeholders.

## Considered options

- Language files beside the exe or through the asset fetch: translators could work without a compiler, but a release is no longer one file, and a first start could show no words at all.
- Windows string resources: not portable, while pure modules must stay ISO C++.

## Consequences

- A new Locale needs a new build and a release. Contributions come as a source file.
- English ships with Korean from the start, because a seam with one implementation proves nothing.
- Server errors already arrive as codes, so they translate here. Release notes stay in the language they were written in.
