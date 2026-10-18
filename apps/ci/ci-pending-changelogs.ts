import * as semver from "https://deno.land/x/semver/mod.ts";

// specify the needed paths here
const CHANGELOG_PATH = "doc/changelog";
const CHANGELOG_PENDING_PATH = `${CHANGELOG_PATH}/pendings`;
const CHANGELOG_MASTER_FILE = `${CHANGELOG_PATH}/master.md`;
const YCORE_JSON = "./acore.json";

// read the acore.json file to work with the versioning
const decoder = new TextDecoder("utf-8");
const data = await Deno.readFile(YCORE_JSON);
const ycoreInfo = JSON.parse(decoder.decode(data));

let changelogText = await Deno.readTextFile(CHANGELOG_MASTER_FILE);

const currentVersion = ycoreInfo.version;

const res=Deno.run({ cmd: [ "git", "rev-parse",
                          "HEAD"],
                   stdout: 'piped',
                   stderr: 'piped',
                   stdin: 'null'  });
await res.status();
const gitVersion = new TextDecoder().decode(await res.output());


for await (const dirEntry of Deno.readDir(CHANGELOG_PENDING_PATH)) {
  if (!dirEntry.isFile || !dirEntry.name.endsWith(".md")) {
    continue;
  }

  // Upgrade the prerelease version number (e.g. 1.0.0-dev.1 -> 1.0.0-dev.2)
  ycoreInfo.version = semver.inc(ycoreInfo.version, "prerelease", {
    includePrerelease: true,
  });

  // read the pending file found and add it at the beginning of the changelog text
  const data = await Deno.readTextFile(
    `${CHANGELOG_PENDING_PATH}/${dirEntry.name}`,
  );
  changelogText = `## ${ycoreInfo.version} | Commit: [${gitVersion}](https://github.com/azerothcore/azerothcore-wotlk/commit/${gitVersion}\n\n${data}\n${changelogText}`;

  // remove the pending file
  await Deno.remove(`${CHANGELOG_PENDING_PATH}/${dirEntry.name}`);
}

// write to acore.json and master.md only if new version is available
if (currentVersion != ycoreInfo.version) {
  console.log(`Changelog version upgraded from ${currentVersion} to ${ycoreInfo.version}`)
  Deno.writeTextFile(CHANGELOG_MASTER_FILE, changelogText);
  Deno.writeTextFile(YCORE_JSON, JSON.stringify(ycoreInfo, null, 2)+"\n");
} else {
  console.log("No changelogs to add")
}
