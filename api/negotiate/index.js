//
// api/negotiate/index.js -- doom-speed-run patch: Race Mode's only
// server-side piece.
//
// Adapted from doom-assist's api/negotiate (same file, same job) -- that
// project needed a relay for its real Doom netcode; this one only ever
// carries small JSON progress/result messages between two racers (see
// raceOpenTransport in web/shell.html), but the token-issuing problem is
// identical either way: a browser can't mint its own Web Pubsub access
// token, since that requires the connection string, a secret this
// function holds (as an app setting) and the browser never sees. So this
// function's entire job is: given a room code, hand back a client access
// URL that's allowed to join and publish to *that one room's* group and
// nothing else. It never sees a single race message once that URL is
// handed out.
//
// This deployment intentionally reuses doom-assist's existing Azure Web
// PubSub resource (same connection string value, set as this app's own
// WEB_PUBSUB_CONNECTION_STRING setting) rather than standing up a second
// resource -- see hubName below for how the two projects' rooms stay
// isolated on that one shared resource.
//
// Deployed as an Azure Static Web Apps "managed function" (see
// .github/workflows/azure-static-web-apps.yml's api_location: "api") --
// reachable at /api/negotiate from the same origin as the game itself, so
// shell.html's fetch() needs no CORS configuration.
//

const { WebPubSubServiceClient } = require("@azure/web-pubsub");

// Set as an SWA "application setting" (Azure Portal, or `az staticwebapp
// appsettings set`) -- the same connection-string value doom-assist's own
// app setting already holds, since this reuses that project's Web PubSub
// resource rather than a new one. Never committed here.
const connectionString = process.env.WEB_PUBSUB_CONNECTION_STRING;

// A hub is Web PubSub's own namespacing concept (roughly: one hub per
// application sharing an instance). This has to differ from doom-assist's
// own "doomassist" hub -- rooms are scoped to a hub, so a different hub
// name is what keeps a doom-speed-run race and a doom-assist netgame from
// ever colliding even though they share the same underlying resource.
const hubName = "doomspeedrun";

module.exports = async function (context, req) {
    if (!connectionString) {
        context.res = {
            status: 500,
            body: "WEB_PUBSUB_CONNECTION_STRING is not configured on this deployment.",
        };
        return;
    }

    const group = (req.query.group || "").trim();

    // Room codes are generated client-side (shell.html's mpGenerateRoomCode)
    // from a fixed short alphanumeric alphabet -- this isn't validating a
    // password, just guarding against something malformed being used as a
    // Web PubSub group name or role string.
    if (!/^[A-Za-z0-9]{1,16}$/.test(group)) {
        context.res = {
            status: 400,
            body: "Invalid or missing 'group' query parameter.",
        };
        return;
    }

    const serviceClient = new WebPubSubServiceClient(connectionString, hubName);

    // Scoped narrowly to this one room: this token can join and publish
    // to *this* group only, not any other room that happens to be active
    // on the same (shared, free-tier) Web PubSub instance at the same
    // time. See learn.microsoft.com's json.webpubsub.azure.v1 subprotocol
    // reference for what these two role strings grant.
    const token = await serviceClient.getClientAccessToken({
        roles: [
            `webpubsub.joinLeaveGroup.${group}`,
            `webpubsub.sendToGroup.${group}`,
        ],
    });

    context.res = {
        status: 200,
        headers: { "Content-Type": "application/json" },
        body: { url: token.url },
    };
};
