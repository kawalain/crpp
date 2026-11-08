// The official Repository's catalog and files are static assets, served without running this.
// Every other path, the root included, is someone looking at the address: show them the source.
// 302, not 301, so that no browser keeps it should a page ever live here.
export default {
  fetch() {
    return Response.redirect("https://github.com/kawalain/crpp", 302);
  },
};
