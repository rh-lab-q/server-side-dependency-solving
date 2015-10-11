/* Server side dependency solving - transfer of dependency solving from local machine to server when installing new packages
 * Copyright (C) 2015  Michal Ruprich, Josef Řídký
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "solving.h"

// HySack* ssds_solve_init()
// {
//   HySack sack = hy_sack_create(NULL, NULL, NULL,HY_MAKE_CACHE_DIR);
//     
//   if(hy_sack_load_system_repo(sack, NULL, HY_BUILD_CACHE))
//     return NULL;
//   
//   HySack* ret;
//   ret = &sack;
//   return ret;
// }

void ssds_fill_sack(HySack* sack, SsdsRepoMetadataList* list)//TODO - sem dostat spravny sack asi pomoci pool_set_installed
{
 // for(ssds_repo::metadata_files_location* loc : metadata.files_locations)
  guint i;
  for(i = 0; i < g_slist_length(list->files_locations); i++)
  {
    SsdsMetadataFilesLoc* file = (SsdsMetadataFilesLoc*)g_slist_nth_data(list->files_locations, i);
    HyRepo repo = hy_repo_create(file->repo_name);
//     ssds_log(logDEBUG, "repomd: %s\n filelists: %s\nprimary: %s\n", file->repomd, file->filelists, file->primary);
    
    int path_len = strlen(file->repomd)+strlen("/repodata/repomd.xml");
    char* repomd_path = (char*)ssds_malloc((path_len+1)*sizeof(char));
    memset(repomd_path, '\0', path_len+1);
    strcpy(repomd_path, file->repomd);
    strcat(repomd_path, "/repodata/repomd.xml");
    
    hy_repo_set_string(repo, HY_REPO_MD_FN, repomd_path);
    hy_repo_set_string(repo, HY_REPO_PRIMARY_FN, file->primary);
    hy_repo_set_string(repo, HY_REPO_FILELISTS_FN, file->filelists);
    hy_repo_set_string(repo, HY_REPO_NAME, file->repo_name);
    
    hy_sack_load_yum_repo(*sack, repo, 0);
    ssds_log(logDEBUG, "One repo loaded to sack.\n");
  }
}

int ssds_dep_query(const char** request, SsdsJsonCreate* answer, HySack* sack, int operation, int pkg_count)
{
	printf("dep_query \n");
	//TODO - pridat moznost erase a upgrade jako typ operace do hlavicky - jestli to jde!!
	HyQuery query = hy_query_create(*sack);
	HyGoal goal = hy_goal_create(*sack);
	HyPackageList plist = hy_packagelist_create();
	HyPackage pkg;
	
	if(operation == GET_UPDATE_ALL)
	{
		printf("get_update_all\n");
// 		hy_query_filter(query, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
		hy_goal_upgrade_all(sack);
		hy_goal_run(goal);
		printf("po run\n");
	}
	else
	{
		if(pkg_count >1)
			hy_query_filter_in(query, HY_PKG_NAME, HY_SUBSTR, request); 
		else
			hy_query_filter(query, HY_PKG_NAME, HY_SUBSTR, request[0]);
		
		
		hy_query_filter_latest_per_arch(query, 1);
			
		/* Getting list of packages from the query */
		plist = hy_query_run(query);
		
		for(int i = 0; i < hy_packagelist_count(plist); i++)
		{
			pkg = hy_packagelist_get(plist, i);
			switch(operation)
			{
				case GET_INSTALL:
					hy_goal_install(goal, pkg);
					break;
				case GET_UPDATE:
					hy_goal_upgrade_to(goal, pkg);
					break;
				case GET_ERASE:
					hy_goal_erase(goal, pkg);
					break;
			}
			if(hy_goal_run(goal) == 0)
				ssds_log(logMESSAGE, "Dependencies for %s are ok.\n", hy_package_get_name(pkg));
		}
	}
	printf("pred insert\n");
  ssds_js_cr_pkgs_insert(answer, &goal);
}


// void ssds_dep_answer(SsdsJsonRead *client_data, SsdsJsonCreate* answer, HySack* sack)
// {
//   ssds_log(logDEBUG,"Dependencies answer.\n");
//   SsdsPkgInfo* pkgs = ssds_js_rd_pkginfo_init();
//   ssds_js_rd_get_packages(pkgs, client_data);
//   
//   ssds_log(logDEBUG,"Answer after get_packages.\n");
//   int i;
//   for(i = 0; i < pkgs->length; i++)
//   {
//     ssds_log(logDEBUG,"Answer in for loop.\n");
//     ssds_dep_query(pkgs->packages[i], answer, sack);
// //     query(pkgs->packages[i], answer);
//   }
// }
