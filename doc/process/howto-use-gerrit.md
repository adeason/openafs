# Gerrit Guide for OpenAFS

## About Gerrit

Patches for OpenAFS are reviewed and accepted a Gerrit Code Review service
hosted by MIT for the OpenAFS project.

Gerrit is an open-source, web-based code review tool, to streamline the process
of reviewing and accepting changes.  This collaborative platform enables teams
to work together on software development projects by supporting peer review and
approval of patches before they are merged into the main codebase.

* **Change submission**: A developer creates one or more a commits in their
  local Git repository.
* **Patch upload**: The developer uploads the commits to Gerrit for review
  using `git push`.
* **Review request**: The commits are assigned a unique identifier (Change-Id),
  and a review request is sent to the designated reviewers.
* **Code review**: Reviewers examine the code, providing feedback in the form
  of comments, questions, or approval votes.
* **Changes and replies**: Authors can address reviewer concerns by making
  changes to the patch or responding with explanations.
* **Approval**: Once all reviewers have approved the patch (or the author has
  addressed their concerns), the patch is considered "approved" and can be
  merged into the codebase by the OpenAFS maintainers.

Gerrit features include:

* **Web-based interface**: Reviewers and authors interact through a web
  interface, making it easy to collaborate remotely.
* **Self-service sign-on**: Sign-on with OpenID and push changes with
  your public ssh key.
* **Change tracking**: Gerrit tracks changes made during the review process,
  allowing for easy comparison of different patch versions.
* **Discussion forums**: Reviewers and authors can engage in threaded
  discussions about specific parts of the code.
* **Customizable workflows**: Teams can define their own review processes and
  approval rules.
* **Integration with Git**: Gerrit is tightly integrated with Git, making it
  simple to create, manage, and merge patches.

## Gerrit workflow

The Gerrit workflow is different than other online Git based project management
systems.  Gerrit is patch-oriented. Patches are logical changes that can be
tracked across revisions of a change.

* Individual patches are submitted instead of **pull requests**.
* A unique **Change-ID** is associated with each patch.
* A shorter, human readable **Gerrit Number** is also assigned to each patch.
* A patchset number is assigned to track revisions of the patch. The patchset
  number of the patch starts at 1 and increments each time the patch is
  updated (usually with `git amend`, followed by `git push`)
* Updates to patches are done with a regular `git push`, `git push --force` is never used.
* Multiple patches in a stack can be submitted in one `git push`.
* An optional **topic** name can be assigned by the submitter when submitting
  patches, which can be helpful to group patches in one stack.


## Getting Started with Gerrit

This guide assumes you are comfortable using Git on the command line.  If you
are new to Git, be sure to take some time to read the Git documentation and
learn the basics.  You should be familiar with the common git commands like
`clone`, `fetch`, and `push`.  Git `rebase` will come in handy when working are
larger changes that require multiple commits.


### Create a local git repository

Start by cloning the OpenAFS git repository:

    $ git clone git://git.openafs.org/openafs.git

Gerrit requires a user name and email for registration, so be sure your
`user.name` and `user.email` in your git cofn Gerrit uses the `user.name` and
`user.email` in your git configuration

Verify your name and email are in the git configuration. Run the following in
your local OpenAFS git repository:

    $ git config --get user.name
    $ git config --get user.email


## Create a Gerrit account

Read-only access to Gerrit is available without creating an account. However,
to submit or review changes, you will need a Gerrit account. To create one,
you will first need an OpenID account.

We recommend using [Launchpad](https://launchpad.net) as your OpenID provider.
Sign up at https://launchpad.net/.

Once you have created your OpenID account, navigate to
https://gerrit.openafs.org and select **Sign In**. You will be redirected to a
page where you can select your OpenID account.  Choose **Launchpad ID**, if you
are using your Launchpad.net account for authentication.

After signing in, access the **Settings** menu and then click on **Contract
Information**. If the email associated with your Gerrit account does not match
the email in your Git configuration, select **Register New Email...** to update
your settings.

## Upload your SSH key

You will need a SSH key pair to upload code changes to Gerrit.  It is
recommended you create password protected SSH key pair specifically for use
with Gerrit.

To create a new ssh key with the default name:

    ssh-keygen -t rsa -f ~/.ssh/id_rsa

The public key will be created as `~/.ssh/id_rsa.pub`.

Upload your public SSH key:

1. Sign-in to [Gerrit](https://gerrit.openafs.org/)
2. Select **Settings**, then **SSH Keys**.
3. Paste your public key value into the **Add SSH Public Key** text area box.
4. Click **Add** to add your public key.
5. Select **Profile** and note the listed `Username`.

## Update your SSH configuation

Add a stanza to your SSH configuration to make it easier to access Gerrit with
`ssh`.

Add the following to your `~/.ssh/config` file:

    Host gerrit.openafs.org
      User <username>
      IdentityFile <key>
      Port 29418
      HostKeyAlgorithms +ssh-rsa
      PubkeyAcceptedAlgorithms +ssh-rsa

Where:

1. `<username>` is the username from the Gerrit **Profile** page.
2. `<key>` is the path to your SSH private key on your local system (e.g.
   `~/.ssh/id_rsa` if you used the default key name.)

## Setup Gerrit Change-Id git hook

Gerrit needs to identify commits that belong to the same logical change. Feor
instance, when a change needs to be modified, a second commit can be uploaded
to address issues reported by code reviewers. Gerrit allows attaching different
commits to the same logical change, and relies upon a `Change-Id` trailer at
the bottom of a commit message. With this `Change-Id`, Gerrit can automatically
associate a new version of a change back to its original review, even across
cherry-picks and rebases.

The `Change-Id` value is generated by the local `commit-msg` git hook when you
create new commits.  This hook generates a `Change-Id` and adds it to the
footer of the commit message when you run `git` commands which create commit
objects (e.g.  `git commit`, `git commit --amend`, `git rebase`, `git
cherry-pick`).

Download the Gerrit `commit-msg` commit hook to your `.git/hooks` directory in
your OpenAFS git repository. Run this command in the top level directory of
your git repository to download the hook:

    $ scp -p -P 29418 gerrit.openafs.org:hooks/commit-msg .git/hooks/

## Submitting commits

See [submitting-changes](submitting-changes.md) for general guidelines.

Upload the commits to Gerrit with `git push`.

To submit changes for the `master` branch:

    $ git push ssh://gerrit.openafs.org/openafs.git HEAD:refs/for/master

TODO:
* topics
* stable branches
