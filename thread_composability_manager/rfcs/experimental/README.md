# Design Documents for Experimental Features

Experimental proposals describe extensions that are implemented and released as
preview features in the library. A preview feature is expected to have an
implementation that is of comparable quality to a fully supported feature.
Sufficient tests are required.

An experimental feature does not yet appear as part of the library interface.
Therefore, the interface and design can change. There is no commitment to
backward compatibility for a preview feature.

The documents in this directory should include a list of the exit conditions
that need to be met to move from preview to fully supported. These conditions
might include demonstrated performance improvements, demonstrated interest from
the community, etc.

For features that require changes in the interface of the library, the document
might include wording for those changes or a link to any PRs that introduce
them.

Proposals should not remain in the experimental directory forever. It should
move either to the supported folder when they become fully supported or the
archived folder if they are not fully accepted. It should be highly unusual for
a proposal to stay in the experimental folder for longer than a year or two.
